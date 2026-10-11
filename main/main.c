#include <assert.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/ledc.h"
#include "esp_heap_caps.h"
#include "esp_lcd_jd9165.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "ui/electric_ui.h"
#include "terminal_board.h"
#include "terminal_video.h"
#include "instrument_frame.h"

static const char *TAG = "moto_instrument";
static esp_lcd_panel_handle_t panel;
static esp_ldo_channel_handle_t phy_power;
static SemaphoreHandle_t refresh_done;
static void *panel_frames[2];
static unsigned back_frame = 1;
static uint16_t *scaled_frame;
static uint16_t *instrument_frame;
static uint16_t source_x[LCD_W];
static uint32_t frames;
static uint64_t scaling_us, scan_wait_us, handler_us, drawing_us;
static uint64_t render_scaling_us;
static int64_t render_start;
static bool backlight_on;
static atomic_bool display_power_requested = true;
static uint32_t edge_hash, edge_changes, edge_samples;
static atomic_uint display_test_request, display_refreshes;
static atomic_uint display_max_gap_us;
static unsigned display_test_mode;

void terminal_display_hold(unsigned seconds)
{
    if (seconds) atomic_store(&display_test_request, 1);
}
void terminal_display_test(unsigned mode) { if (mode >= 1 && mode <= 3) atomic_store(&display_test_request, mode); }

void terminal_display_set_power(bool powered) { atomic_store(&display_power_requested, powered); }

/* The display owner alone touches LEDC. Fake power leaves scanout, networking
 * and state reporting alive, and never lights an uninitialized framebuffer. */
static void sync_backlight(void)
{
    bool powered = atomic_load(&display_power_requested) && frames > 0;
    if (powered == backlight_on) return;
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, powered ? 818 : 0));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
    backlight_on = powered;
    ESP_LOGI(TAG, "DEMO_DISPLAY_POWER powered=%d; Wi-Fi/BLE remain available", powered);
}

/* The outer four pixels contain static artwork. Track changes in submitted
 * instrument frames to distinguish software edge flicker from LCD scanout.
 * A theme/style transition can legitimately change this fingerprint. */
static void check_instrument_edges(const uint16_t *pixels)
{
    uint32_t hash = 2166136261u;
    for (unsigned y = 0; y < LCD_H; ++y) {
        const uint16_t *row = pixels + y * LCD_W;
        if (y < 4 || y >= LCD_H - 4) {
            for (unsigned x = 0; x < LCD_W; ++x) hash = (hash ^ row[x]) * 16777619u;
        } else {
            for (unsigned x = 0; x < 4; ++x) {
                hash = (hash ^ row[x]) * 16777619u;
                hash = (hash ^ row[LCD_W - 1 - x]) * 16777619u;
            }
        }
    }
    if (edge_samples && hash != edge_hash) ++edge_changes;
    edge_hash = hash;
    ++edge_samples;
}

static uint32_t tick_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void rendering_started(lv_event_t *event)
{
    (void)event;
    render_start = esp_timer_get_time();
    render_scaling_us = 0;
}

static bool refresh_complete(esp_lcd_panel_handle_t handle,
                              esp_lcd_dpi_panel_event_data_t *event, void *context)
{
    (void)handle;
    (void)event;
    BaseType_t wake = pdFALSE;
    static uint32_t previous_us;
    uint32_t now_us = (uint32_t)esp_timer_get_time();
    if (previous_us) {
        unsigned gap = now_us - previous_us;
        unsigned maximum = atomic_load_explicit(&display_max_gap_us, memory_order_relaxed);
        while (gap > maximum && !atomic_compare_exchange_weak_explicit(
                   &display_max_gap_us, &maximum, gap, memory_order_relaxed, memory_order_relaxed)) {}
    }
    previous_us = now_us;
    atomic_fetch_add_explicit(&display_refreshes, 1, memory_order_relaxed);
    xSemaphoreGiveFromISR((SemaphoreHandle_t)context, &wake);
    return wake == pdTRUE;
}

static void present_frame(void)
{
    if (display_test_mode != 3) terminal_board_overlay(scaled_frame, LCD_W, LCD_H);
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, 0, LCD_W, LCD_H, scaled_frame));
    xSemaphoreTake(refresh_done, 0);
    for (unsigned i = 0; i < 2; ++i) {
        if (xSemaphoreTake(refresh_done, pdMS_TO_TICKS(1000)) != pdTRUE) {
            ESP_LOGE(TAG, "LCD refresh timeout");
            abort();
        }
    }
    back_frame ^= 1;
    scaled_frame = panel_frames[back_frame];
    ++frames;
    sync_backlight();
}

static bool instrument_base_ready;

static void flush_frame(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    if (terminal_video_visible() && !terminal_video_preview()) { lv_display_flush_ready(display); return; }
    int64_t start = esp_timer_get_time();
    bool cockpit = electric_ui_style() == ELECTRIC_STYLE_COCKPIT;
    if (!instrument_base_ready || (area->x1 == 0 && area->y1 == 0 &&
                                  area->x2 == ART_W-1 && area->y2 == ART_H-1)) {
        if (cockpit) instrument_frame_compose_full(instrument_frame, (const uint16_t *)pixels, source_x);
        else instrument_frame_compose(instrument_frame, (const uint16_t *)pixels, source_x);
    }
    else
        instrument_frame_update(instrument_frame, (const uint16_t *)pixels, source_x, cockpit,
                                area->x1, area->y1, area->x2, area->y2);
    instrument_base_ready = true;
    uint64_t scale_elapsed = esp_timer_get_time() - start;
    scaling_us += scale_elapsed;
    render_scaling_us += scale_elapsed;
    if (!lv_display_flush_is_last(display)) { lv_display_flush_ready(display); return; }
    drawing_us += esp_timer_get_time() - render_start - render_scaling_us;
    // Every submitted LCD buffer receives the complete retained composition.
    // Notifications are drawn on the back buffer only, never into this base.
    uint32_t map_stream=0; uint64_t map_frame=0; unsigned map_generation=0;
    bool live_map=cockpit && terminal_video_copy_map(instrument_frame,false,&map_stream,&map_frame,&map_generation);
    memcpy(scaled_frame,instrument_frame,LCD_W*LCD_H*2);
    if (display_test_mode == 2) { lv_display_flush_ready(display); return; }
    start = esp_timer_get_time();
    // Discard an old VSYNC, then allow a complete scan after the buffer swap.
    // This keeps the previous front buffer untouched until the LCD has left it.
    present_frame();
    check_instrument_edges(panel_frames[back_frame ^ 1]);
    terminal_board_instrument_presented();
    if(live_map && map_stream) terminal_video_presented(map_stream,map_frame,map_generation);
    scan_wait_us += esp_timer_get_time() - start;
    lv_display_flush_ready(display);
}

static void start_panel(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_1, .freq_hz = 20000, .clk_cfg = LEDC_AUTO_CLK,
    };
    const ledc_channel_config_t light = {
        .gpio_num = 23, .speed_mode = LEDC_LOW_SPEED_MODE, .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_1, .duty = 0,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ESP_ERROR_CHECK(ledc_channel_config(&light));
    const esp_ldo_channel_config_t power = {.chan_id = 3, .voltage_mv = 2500};
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&power, &phy_power));

    esp_lcd_dsi_bus_handle_t bus;
    esp_lcd_dsi_bus_config_t bus_config = JD9165_PANEL_BUS_DSI_2CH_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_config, &bus));
    esp_lcd_panel_io_handle_t io;
    esp_lcd_dbi_io_config_t io_config = JD9165_PANEL_IO_DBI_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(bus, &io_config, &io));
    esp_lcd_dpi_panel_config_t dpi = JD9165_1024_600_PANEL_60HZ_DPI_CONFIG(LCD_COLOR_PIXEL_FORMAT_RGB565);
    dpi.num_fbs = 2;
    dpi.flags.use_dma2d = false;
    jd9165_vendor_config_t vendor = {.mipi_config = {.dsi_bus = bus, .dpi_config = &dpi}};
    const esp_lcd_panel_dev_config_t device = {
        .reset_gpio_num = 0, .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16, .vendor_config = &vendor,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_jd9165(io, &device, &panel));
    refresh_done = xSemaphoreCreateBinary();
    assert(refresh_done);
    const esp_lcd_dpi_panel_event_callbacks_t callbacks = {.on_refresh_done = refresh_complete};
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_register_event_callbacks(panel, &callbacks, refresh_done));
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_get_frame_buffer(panel, 2, &panel_frames[0], &panel_frames[1]));
    memset(panel_frames[0], 0, LCD_W * LCD_H * 2);
    memset(panel_frames[1], 0, LCD_W * LCD_H * 2);
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_LOGI(TAG, "JD9165 New Panel/V2 initialized: 1024x600 RGB565, DSI 750Mbps, pixel clock 52MHz");
}

static void instrument_task(void *context)
{
    (void)context;
    lv_init();
    lv_tick_set_cb(tick_ms);
    lv_display_t *display = lv_display_create(ART_W, ART_H);
    assert(display);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    void *draw_buffer = heap_caps_aligned_alloc(64, ART_W * ART_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    instrument_frame = heap_caps_aligned_alloc(64, LCD_W * LCD_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    scaled_frame = panel_frames[back_frame];
    assert(draw_buffer && scaled_frame && instrument_frame);
    memset(draw_buffer, 0, ART_W * ART_H * 2);
    memset(instrument_frame, 0, LCD_W * LCD_H * 2);
    memset(scaled_frame, 0, LCD_W * LCD_H * 2);
    for (unsigned x = 0; x < LCD_W; ++x) source_x[x] = x * ART_W / LCD_W;
    lv_display_set_buffers(display, draw_buffer, NULL, ART_W * ART_H * 2, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display, flush_frame);
    lv_display_add_event_cb(display, rendering_started, LV_EVENT_REFR_START, NULL);
    electric_ui_create(lv_screen_active());
    terminal_board_create();
    ESP_LOGI(TAG, "FreeRTOS + LVGL %d.%d.%d instrument started; App-controlled theme, electric DEMO drive cycle 60s",
             LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);

    uint32_t report_tick = tick_ms(), report_frames = 0;
    unsigned report_refreshes = atomic_load(&display_refreshes);
    bool was_video = false;
    uint32_t hold_until = 0, hold_refreshes = 0;
    for (;;) {
        terminal_board_tick();
        sync_backlight();
        unsigned request = atomic_exchange(&display_test_request, 0);
        if (request && !terminal_video_active() && backlight_on) {
            display_test_mode = request;
            hold_until = tick_ms() + 30000;
            hold_refreshes = atomic_load(&display_refreshes);
            if (request == 3) memcpy(scaled_frame,panel_frames[back_frame ^ 1],LCD_W*LCD_H*2);
            ESP_LOGI(TAG, "DISPLAY_TEST begin mode=%u duration_ms=30000", request);
        }
        if (hold_until) {
            if ((int32_t)(hold_until - tick_ms()) > 0 && !terminal_video_active()) {
                // Diagnostic only: preserve the last submitted framebuffer while
                // the LCD DMA/VSYNC and the BLE/Wi-Fi tasks continue normally.
                if (display_test_mode == 1 || display_test_mode == 3) {
                    if (display_test_mode == 3) present_frame();
                    vTaskDelay(pdMS_TO_TICKS(display_test_mode == 3 ? 200 : 10));
                    continue;
                }
            } else {
                ESP_LOGI(TAG, "DISPLAY_TEST end mode=%u refreshes=%u", display_test_mode, atomic_load(&display_refreshes) - hold_refreshes);
                hold_until = 0;
                display_test_mode = 0;
                lv_obj_invalidate(lv_screen_active());
                lv_timer_ready(lv_display_get_refr_timer(display));
            }
        }
        uint32_t video_stream; uint64_t video_frame; unsigned video_generation;
        bool video = terminal_video_visible() && !terminal_video_preview();
        if (video != was_video) {
            lv_timer_t *refresh = lv_display_get_refr_timer(display);
            /* Invalidation normally resumes LVGL's refresh timer. Suppress it
             * while video covers the display, then redraw all current state. */
            lv_display_enable_invalidation(display, !video);
            if (video) lv_timer_pause(refresh);
            else {
                lv_timer_resume(refresh);
                lv_obj_invalidate(lv_screen_active());
                lv_timer_ready(refresh);
            }
        }
        bool overlay_changed = terminal_board_overlay_changed();
        if (overlay_changed && !video && instrument_base_ready && display_test_mode != 2) {
            memcpy(scaled_frame,instrument_frame,LCD_W*LCD_H*2);
            present_frame();
        }
        if (!video && instrument_base_ready && electric_ui_style() == ELECTRIC_STYLE_COCKPIT &&
            terminal_video_copy_map(instrument_frame,true,&video_stream,&video_frame,&video_generation)) {
            memcpy(scaled_frame,instrument_frame,LCD_W*LCD_H*2);
            present_frame();
            terminal_video_presented(video_stream,video_frame,video_generation);
        } else if (video && terminal_video_copy(scaled_frame, &video_stream, &video_frame, &video_generation)) {
            instrument_base_ready=false;
            present_frame();
            terminal_video_presented(video_stream, video_frame, video_generation);
        } else if (video && overlay_changed && terminal_video_copy_current(scaled_frame)) {
            present_frame();
        }
        was_video = video;
        int64_t handler_start = esp_timer_get_time();
        uint32_t delay = lv_timer_handler();
        handler_us += esp_timer_get_time() - handler_start;
        uint32_t now = tick_ms();
        if (now - report_tick >= 5000) {
            electric_state_t state = terminal_board_state();
            ESP_LOGI(TAG, "render %.1f fps; frames=%" PRIu32 "; speed=%u battery=%u%%; free PSRAM=%u, internal=%u, stack=%u",
                     (frames - report_frames) * 1000.0f / (now - report_tick), frames,
                     state.speed_kph, state.battery_percent,
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)uxTaskGetStackHighWaterMark(NULL));
            unsigned drawn = frames - report_frames;
            if (drawn) ESP_LOGI(TAG, "frame costs: handler=%.1fms draw=%.1fms scale=%.1fms scan_wait=%.1fms",
                               handler_us / (1000.0f * drawn), drawing_us / (1000.0f * drawn), scaling_us / (1000.0f * drawn),
                               scan_wait_us / (1000.0f * drawn));
            ESP_LOGI(TAG, "instrument edges: samples=%" PRIu32 " changes=%" PRIu32 " hash=%08" PRIx32,
                     edge_samples, edge_changes, edge_hash);
            unsigned refreshes = atomic_load(&display_refreshes);
            ESP_LOGI(TAG, "LCD scan %.1fHz; max_gap_us=%u; cache_writeback_chunk=8192",
                     (refreshes-report_refreshes)*1000.0f/(now-report_tick), atomic_exchange(&display_max_gap_us, 0));
            report_refreshes = refreshes;
            handler_us = drawing_us = scaling_us = scan_wait_us = 0;
            report_tick = now;
            report_frames = frames;
        }
        vTaskDelay(pdMS_TO_TICKS(delay > 10 ? 10 : delay < 1 ? 1 : delay));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Moto instrument demo; data is simulated");
    assert(hud_display_init());
    hud_display_set_provisioning(true);
    start_panel();
    BaseType_t created = xTaskCreatePinnedToCore(instrument_task, "instrument", 24576, NULL, 4, NULL, 1);
    assert(created == pdPASS);
    p4_terminal_start();
}
