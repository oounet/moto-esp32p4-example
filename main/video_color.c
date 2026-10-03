#include "video_color.h"
#include "video_color_pack.h"
#include <stdlib.h>
#include "driver/ppa.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "VideoColor";
static ppa_client_handle_t client;
static uint8_t *packed;
static unsigned width, height;
static uint64_t pack_us, ppa_us, color_frames;

static uint8_t clamp(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }
static uint16_t pixel(uint8_t y, uint8_t u, uint8_t v)
{
    int yy = y - 16, uu = u - 128, vv = v - 128;
    uint8_t r = clamp((298 * yy + 409 * vv + 128) >> 8);
    uint8_t g = clamp((298 * yy - 100 * uu - 208 * vv + 128) >> 8);
    uint8_t b = clamp((298 * yy + 516 * uu + 128) >> 8);
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}
static void software(const uint8_t *src, unsigned coded_height, uint16_t *out)
{
    const uint8_t *u = src + width * coded_height, *v = u + width * coded_height / 4;
    for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x)
        out[y * width + x] = pixel(src[y * width + x], u[y / 2 * (width / 2) + x / 2],
                                  v[y / 2 * (width / 2) + x / 2]);
}
static esp_err_t hardware(const uint8_t *src, unsigned coded_height, uint16_t *out)
{
    int64_t start = esp_timer_get_time();
    video_color_pack(src, width, coded_height, height, packed);
    pack_us += esp_timer_get_time() - start;
    ppa_srm_oper_config_t op = {
        .in = {.buffer = packed, .pic_w = width, .pic_h = height,
            .block_w = width, .block_h = height, .srm_cm = PPA_SRM_COLOR_MODE_YUV420,
            .yuv_range = PPA_COLOR_RANGE_LIMIT, .yuv_std = PPA_COLOR_CONV_STD_RGB_YUV_BT601},
        .out = {.buffer = out, .buffer_size = width * height * 2,
            .pic_w = width, .pic_h = height, .srm_cm = PPA_SRM_COLOR_MODE_RGB565},
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_0, .scale_x = 1, .scale_y = 1,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };
    /* The driver writes back packed input and invalidates aligned DMA output. */
    start = esp_timer_get_time();
    esp_err_t result = ppa_do_scale_rotate_mirror(client, &op);
    ppa_us += esp_timer_get_time() - start;
    ++color_frames;
    return result;
}
static void disable(void)
{
    if (client) { ppa_unregister_client(client); client = NULL; }
    heap_caps_free(packed); packed = NULL;
}
void video_color_init(unsigned w, unsigned h, uint16_t *out)
{
    width = w; height = h;
    ppa_client_config_t config = {.oper_type = PPA_OPERATION_SRM, .max_pending_trans_num = 1};
    packed = heap_caps_aligned_alloc(128, width * height * 3 / 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    if (!packed || ppa_register_client(&config, &client) != ESP_OK) {
        disable(); ESP_LOGW(TAG, "PPA unavailable; using CPU conversion"); return;
    }
    /* Check every pixel at actual display dimensions, including the 608->600
     * crop. Varied luma/chroma catch plane order, row parity, stride and DMA
     * block-edge errors. Do not expose an unverified hardware image to LCD. */
    unsigned coded_height = (height + 15) & ~15u;
    unsigned plane = width * coded_height;
    uint8_t *sample = heap_caps_malloc(plane * 3 / 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!sample) { disable(); ESP_LOGW(TAG, "PPA self-test allocation failed; using CPU"); return; }
    for (unsigned y = 0; y < coded_height; ++y) for (unsigned x = 0; x < width; ++x)
        sample[y * width + x] = 16 + (x * 13 + y * 7) % 220;
    for (unsigned y = 0; y < coded_height / 2; ++y) for (unsigned x = 0; x < width / 2; ++x) {
        sample[plane + y * (width / 2) + x] = 16 + (x * 3 + y * 17) % 225;
        sample[plane * 5 / 4 + y * (width / 2) + x] = 16 + (x * 19 + y * 5) % 225;
    }
    int64_t start = esp_timer_get_time();
    esp_err_t rc = hardware(sample, coded_height, out);
    int64_t hardware_us = esp_timer_get_time() - start;
    unsigned mismatches = 0, max_error = 0;
    if (rc == ESP_OK) {
        for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x) {
            unsigned chroma = y / 2 * (width / 2) + x / 2;
            uint16_t expected = pixel(sample[y * width + x], sample[plane + chroma], sample[plane * 5 / 4 + chroma]);
            uint16_t got = out[y * width + x];
            unsigned dr = abs((int)(expected >> 11) - (int)(got >> 11));
            unsigned dg = abs((int)((expected >> 5) & 63) - (int)((got >> 5) & 63));
            unsigned db = abs((int)(expected & 31) - (int)(got & 31));
            unsigned error = dr > dg ? dr : dg; if (db > error) error = db;
            if (error > max_error) max_error = error;
            if (error > 1) ++mismatches; /* Hardware coefficient rounding differs by <=1 RGB565 step. */
        }
    }
    start = esp_timer_get_time(); software(sample, coded_height, out);
    int64_t software_us = esp_timer_get_time() - start;
    heap_caps_free(sample);
    bool valid = rc == ESP_OK && !mismatches;
    ESP_LOGI(TAG, "PPA_SELFTEST passed=%d mismatches=%u max_rgb565_error=%u ppa_us=%lld cpu_us=%lld",
             valid, mismatches, max_error, hardware_us, software_us);
    pack_us = ppa_us = color_frames = 0;
    if (!valid) disable();
}
void video_color_convert(const uint8_t *src, unsigned coded_height, uint16_t *out)
{
    if (client) {
        if (hardware(src, coded_height, out) == ESP_OK) {
            if (color_frames == 60) {
                ESP_LOGI(TAG, "COLOR_STATS pack_us=%llu ppa_us=%llu",
                    (unsigned long long)(pack_us / color_frames), (unsigned long long)(ppa_us / color_frames));
                pack_us = ppa_us = color_frames = 0;
            }
            return;
        }
        ESP_LOGE(TAG, "PPA conversion failed; switching to CPU"); disable();
    }
    software(src, coded_height, out);
}
