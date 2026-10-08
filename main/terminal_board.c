#include "terminal_board.h"
#include "terminal_video.h"
#include "notice_view.h"
#include "hud_view.h"
#include "route_map_store.h"
#include "media_session.h"
#include "instrument_frame.h"
#include "moto/ancs.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "nvs.h"
#include "nvs_flash.h"

typedef struct {
    bool provisioning, connected, pin_visible, night, phone_known, hud_valid;
    unsigned pin;
    int64_t pin_until, hud_until;
    moto_hud hud;
    terminal_notice notice;
} board_state;
static board_state state;
static bool navigation_active;
static uint64_t hud_generation;
static SemaphoreHandle_t mutex;
static QueueHandle_t commands, results;
static atomic_uint control_epoch;
static atomic_bool media_active;
static atomic_int applied_instrument = ELECTRIC_STYLE_COCKPIT;
static SemaphoreHandle_t instrument_mutex;
static nvs_handle_t instrument_storage;
static bool instrument_available;
static uint32_t instrument_revision = 1;
static int instrument_target = ELECTRIC_STYLE_COCKPIT;
static int instrument_result = MOTO_OK;
static bool instrument_rendering, instrument_rollback;
static int64_t instrument_deadline;
static const char *style_id(int style) { return style == ELECTRIC_STYLE_COCKPIT ? "cockpit" : "classic"; }
const char *terminal_board_instrument(void) { return style_id(atomic_load(&applied_instrument)); }
static int instrument_read(void *user, moto_instrument_state *out) {
    (void)user;
    if (!instrument_available) return MOTO_UNSUPPORTED;
    if (!xSemaphoreTake(instrument_mutex, 0)) return MOTO_BUSY;
    memset(out, 0, sizeof *out);
    out->revision = instrument_revision; out->count = 2;
    strcpy(out->current, terminal_board_instrument());
    strcpy(out->installed[0], "classic"); strcpy(out->installed[1], "cockpit");
    out->busy = instrument_result == MOTO_AGAIN || instrument_rollback;
    xSemaphoreGive(instrument_mutex); return MOTO_OK;
}
static int instrument_select(void *user, const char *id, uint32_t revision) {
    (void)user;
    int target;
    if (!id) return MOTO_INVALID;
    if (!strcmp(id, "classic")) target = ELECTRIC_STYLE_CLASSIC;
    else if (!strcmp(id, "cockpit")) target = ELECTRIC_STYLE_COCKPIT;
    else return MOTO_UNSUPPORTED;
    if (!instrument_available) return MOTO_UNSUPPORTED;
    if (!xSemaphoreTake(instrument_mutex, 0)) return MOTO_BUSY;
    int rc = MOTO_AGAIN;
    if (instrument_result == MOTO_AGAIN || instrument_rollback || atomic_load(&media_active) || terminal_video_visible()) rc = MOTO_BUSY;
    else if (revision != instrument_revision || revision == UINT32_MAX) rc = MOTO_STATE;
    else {
        instrument_target = target; instrument_rendering = false;
        instrument_deadline = esp_timer_get_time() / 1000 + 4000;
        instrument_result = MOTO_AGAIN;
    }
    xSemaphoreGive(instrument_mutex); return rc;
}
static int instrument_poll(void *user) {
    (void)user;
    if (!instrument_available) return MOTO_UNSUPPORTED;
    if (!xSemaphoreTake(instrument_mutex, 0)) return MOTO_AGAIN;
    int rc = instrument_rollback ? MOTO_AGAIN : instrument_result;
    xSemaphoreGive(instrument_mutex); return rc;
}
moto_instrument_port terminal_board_instrument_port(void) {
    if (!instrument_available) return (moto_instrument_port){0};
    return (moto_instrument_port){NULL, instrument_read, instrument_select, instrument_poll};
}
bool terminal_board_select_instrument(const char *id) {
    moto_instrument_state snapshot;
    if (instrument_read(NULL, &snapshot) != MOTO_OK || snapshot.busy) return false;
    if (id && !strcmp(snapshot.current, id)) return true;
    return instrument_select(NULL, id, snapshot.revision) == MOTO_AGAIN;
}
/* Called only after the full LVGL frame has reached the LCD, never for video/overlay copies. */
void terminal_board_instrument_presented(void) {
    if (!instrument_available || !xSemaphoreTake(instrument_mutex, 0)) return;
    if (instrument_rendering && instrument_result == MOTO_AGAIN) {
        uint32_t next = instrument_revision + 1;
        const uint8_t record[] = {1, (uint8_t)instrument_target, (uint8_t)(next >> 24),
            (uint8_t)(next >> 16), (uint8_t)(next >> 8), (uint8_t)next};
        esp_err_t rc = nvs_set_blob(instrument_storage, "selection", record, sizeof record);
        if (rc == ESP_OK) rc = nvs_commit(instrument_storage);
        if (rc == ESP_OK) {
            atomic_store(&applied_instrument, instrument_target);
            instrument_revision = next; instrument_result = MOTO_OK;
            printf("MOTO_INSTRUMENT_APPLIED style=%s revision=%lu\n", terminal_board_instrument(), (unsigned long)next);
        } else { instrument_result = MOTO_IO; instrument_rollback = true; }
        instrument_rendering = false;
    }
    xSemaphoreGive(instrument_mutex);
}
static lv_obj_t *status_label, *pin_label, *hud_label, *control_label;
static bool theme_applied, last_night;
static electric_state_t telemetry;
static uint32_t last_sample;
electric_state_t terminal_board_state(void) { return telemetry; }

bool hud_display_init(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    instrument_mutex = xSemaphoreCreateMutex();
    instrument_available = instrument_mutex && nvs_open("instrument", NVS_READWRITE, &instrument_storage) == ESP_OK;
    if (instrument_available) {
        uint8_t record[6]; size_t length = sizeof record;
        if (nvs_get_blob(instrument_storage, "selection", record, &length) == ESP_OK &&
            length == sizeof record && record[0] == 1 && record[1] <= ELECTRIC_STYLE_COCKPIT) {
            uint32_t revision = ((uint32_t)record[2] << 24) | ((uint32_t)record[3] << 16) |
                ((uint32_t)record[4] << 8) | record[5];
            if (revision) { atomic_store(&applied_instrument, record[1]); instrument_revision = revision; }
        }
    }
    mutex = xSemaphoreCreateMutex();
    commands = xQueueCreate(8, sizeof(control_effect));
    results = xQueueCreate(8, sizeof(control_effect));
    return mutex && commands && results;
}
void hud_display_set_provisioning(bool required)
{
    xSemaphoreTake(mutex, portMAX_DELAY);
    state.provisioning = required;
    xSemaphoreGive(mutex);
}
void hud_display_event(const moto_event *event)
{
    int64_t now = esp_timer_get_time() / 1000;
    xSemaphoreTake(mutex, portMAX_DELAY);
    switch (event->kind) {
    case MOTO_EVENT_NAV_BEGIN:
        atomic_store(&media_active, true);
        navigation_active = terminal_session_has_navigation_hud(event->body);
        state.hud_valid = false;
        ++hud_generation;
        break;
    case MOTO_EVENT_PAIRING_PIN:
        state.pin = event->value; state.pin_visible = true;
        state.pin_until = now + event->target * 1000;
        break;
    case MOTO_EVENT_PAIRING_CLEARED:
    case MOTO_EVENT_PAIR: state.pin_visible = false; state.pin = 0; break;
    case MOTO_EVENT_AUTHENTICATED: state.connected = true; break;
    case MOTO_EVENT_CLOSED:
    case MOTO_EVENT_UNPAIR:
        atomic_store(&media_active, false);
        navigation_active=false; ++hud_generation;
        state.connected = false; state.pin_visible = false; state.pin = 0;
        state.hud_valid = false; state.phone_known = false;
        memset(&state.notice, 0, sizeof state.notice); break;
    case MOTO_EVENT_PHONE_STATE:
        state.phone_known = true; state.night = event->phone->theme == MOTO_THEME_NIGHT; break;
    case MOTO_EVENT_HUD:
        ++hud_generation;
        state.hud = *event->hud; state.hud_valid = true;
        state.hud_until = now + event->hud->ttl_ms; break;
    case MOTO_EVENT_HUD_EXPIRED: state.hud_valid = false; ++hud_generation; break;
    case MOTO_EVENT_NAV_END: atomic_store(&media_active, false); navigation_active=false; ++hud_generation; state.hud_valid = false; break;
    case MOTO_EVENT_NOTIFICATION: {
        moto_slice field; uint64_t ttl = 0;
        memset(&state.notice, 0, sizeof state.notice);
        if (moto_cbor_find(event->body, 0, &field)) moto_cbor_get_uint(field, &state.notice.id);
        if (moto_cbor_find(event->body, 1, &field)) moto_cbor_get_uint(field, &ttl);
        char *out[] = {state.notice.app, state.notice.title, state.notice.text};
        const size_t sizes[] = {sizeof state.notice.app, sizeof state.notice.title, sizeof state.notice.text};
        for (unsigned i=0; i<3; ++i)
            if (moto_cbor_find(event->body, i+3, &field))
                moto_cbor_get_text(field, out[i], sizes[i]);
        state.notice.until = now + (ttl < 4000 ? ttl : 4000); break;
    }
    case MOTO_EVENT_NOTIFICATION_REMOVE: {
        moto_slice field; uint64_t id = 0;
        if (moto_cbor_find(event->body, 0, &field) && moto_cbor_get_uint(field, &id) &&
            !state.notice.ancs && state.notice.id == id)
            memset(&state.notice, 0, sizeof state.notice);
        break;
    }
    default: break;
    }
    xSemaphoreGive(mutex);
}
static lv_obj_t *label(lv_align_t align, int x, int y)
{
    lv_obj_t *object = lv_label_create(lv_layer_top());
    lv_obj_set_style_bg_color(object, lv_color_hex(0x11212f), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_90, 0);
    lv_obj_set_style_text_color(object, lv_color_white(), 0);
    lv_obj_set_style_pad_all(object, 10, 0);
    lv_obj_align(object, align, x, y);
    return object;
}
void terminal_board_create(void)
{
    electric_ui_set_style((electric_style_t)atomic_load(&applied_instrument));
    status_label = label(LV_ALIGN_TOP_LEFT, 190, 28);
    pin_label = label(LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(pin_label, &lv_font_montserrat_48, 0);
    hud_label = label(LV_ALIGN_BOTTOM_MID, 0, -66);
    control_label = label(LV_ALIGN_TOP_RIGHT, -180, 28);
    lv_label_set_text(control_label, "DEMO");
    lv_obj_set_style_bg_opa(status_label, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(control_label, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(status_label, 4, 0);
    lv_obj_set_style_pad_all(control_label, 4, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x454872), 0);
    lv_obj_set_style_text_color(control_label, lv_color_hex(0x454872), 0);
}
void terminal_board_tick(void)
{
    board_state copy;
    xSemaphoreTake(mutex, portMAX_DELAY); copy = state; xSemaphoreGive(mutex);
    int64_t now = esp_timer_get_time() / 1000;
    if (instrument_available && xSemaphoreTake(instrument_mutex, 0)) {
        if (instrument_rollback) {
            electric_ui_set_style((electric_style_t)atomic_load(&applied_instrument));
            instrument_rollback = false;
        }
        if (instrument_result == MOTO_AGAIN && !instrument_rendering) {
            if (now >= instrument_deadline) instrument_result = MOTO_EXPIRED;
            else if (terminal_video_visible() || copy.hud_valid) instrument_result = MOTO_BUSY;
            else {
                electric_ui_set_style((electric_style_t)instrument_target);
                instrument_rendering = true;
                lv_obj_invalidate(lv_screen_active());
            }
        }
        xSemaphoreGive(instrument_mutex);
    }
    bool cockpit=electric_ui_style()==ELECTRIC_STYLE_COCKPIT;
    lv_obj_set_flag(status_label,LV_OBJ_FLAG_HIDDEN,cockpit&&!copy.provisioning);
    lv_obj_set_flag(control_label,LV_OBJ_FLAG_HIDDEN,cockpit&&!strcmp(lv_label_get_text(control_label),"DEMO"));
    // Keep feedback within the tighter cockpit viewport, in the free header
    // center rather than the removed bottom padding or status-light row.
    if(cockpit)lv_obj_align(control_label,LV_ALIGN_TOP_MID,0,COCKPIT_TOP+8);
    else lv_obj_align(control_label,LV_ALIGN_TOP_RIGHT,-180,28);
    const char *status = copy.provisioning ? "LICENSE REQUIRED" :
                         copy.connected ? "APP CONNECTED" : "APP OFFLINE";
    if (strcmp(lv_label_get_text(status_label), status)) lv_label_set_text(status_label, status);
    bool show_pin = copy.pin_visible && now < copy.pin_until;
    lv_obj_set_flag(pin_label, LV_OBJ_FLAG_HIDDEN, !show_pin);
    if (show_pin) {
        char text[64]; snprintf(text, sizeof text, "Pairing PIN\n%04u", copy.pin);
        if (strcmp(lv_label_get_text(pin_label), text)) lv_label_set_text(pin_label, text);
    } else if (lv_label_get_text(pin_label)[0]) lv_label_set_text(pin_label, "");
    if (copy.phone_known && (!theme_applied || last_night != copy.night)) {
        electric_ui_set_night(copy.night);
        lv_color_t ink=lv_color_hex(copy.night ? 0xaaa9ac : 0x454872);
        lv_obj_set_style_text_color(status_label, ink, 0);
        lv_obj_set_style_text_color(control_label, ink, 0);
        theme_applied = true; last_night = copy.night;
    }
    if (!copy.phone_known) theme_applied = false;
    if (!terminal_video_visible() && lv_tick_elaps(last_sample) >= 50) {
        last_sample = lv_tick_get();
        telemetry = electric_demo_sample((uint32_t)now);
        telemetry.connected = copy.connected;
        electric_ui_set_state(&telemetry);
    }
    char text[192] = "";
    if (copy.hud_valid && now < copy.hud_until)
        snprintf(text, sizeof text, "Navigation: action %lu | %lu m | remaining %lu m / %lu min",
                 (unsigned long)copy.hud.action, (unsigned long)copy.hud.distance_m,
                 (unsigned long)copy.hud.remaining_m, (unsigned long)(copy.hud.remaining_s / 60));

    if (strcmp(lv_label_get_text(hud_label), text)) lv_label_set_text(hud_label, text);
    lv_obj_set_flag(hud_label, LV_OBJ_FLAG_HIDDEN, !text[0]);
    control_effect command;
    while (uxQueueSpacesAvailable(results) && xQueueReceive(commands, &command, 0) == pdTRUE) {
        if (command.epoch != atomic_load(&control_epoch)) continue;
        command.success = now < (int64_t)command.deadline_ms;
        if (command.success) {
            static const char *names[] = {"Lock", "Volume", "Light", "Horn"};
            snprintf(text, sizeof text, "DEMO %s: %lu", names[command.message - MOTO_LOCK_SET], (unsigned long)command.value);
            lv_label_set_text(control_label, text);
        }
        xQueueSend(results, &command, 0);
    }
}
void control_effects_init(void) { /* Queues initialized before the display task. */ }
bool control_effects_audio_ready(void) { return false; }
bool control_effects_light_ready(void) { return false; }
bool control_effects_submit(control_effect effect)
{
    if (effect.message < MOTO_LOCK_SET || effect.message > MOTO_HORN_TRIGGER) return false;
    effect.epoch = atomic_load(&control_epoch);
    return xQueueSend(commands, &effect, 0) == pdTRUE;
}
bool control_effects_result(control_effect *effect)
{
    while (xQueueReceive(results, effect, 0) == pdTRUE)
        if (effect->epoch == atomic_load(&control_epoch)) return true;
    return false;
}
void control_effects_cancel(void) { atomic_fetch_add(&control_epoch, 1); }

void hud_display_ancs(const moto_ancs_notification *notice) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    memset(&state.notice, 0, sizeof state.notice);
    state.notice.id = notice->uid; state.notice.ancs = true;
    state.notice.until = esp_timer_get_time()/1000 + 4000;
    snprintf(state.notice.app, sizeof state.notice.app, "%s", notice->app);
    snprintf(state.notice.title, sizeof state.notice.title, "%s", notice->title);
    snprintf(state.notice.text, sizeof state.notice.text, "%s", notice->text);
    xSemaphoreGive(mutex);
}
void hud_display_ancs_remove(uint32_t uid) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (state.notice.ancs && state.notice.id == uid) memset(&state.notice, 0, sizeof state.notice);
    xSemaphoreGive(mutex);
}
void hud_display_ancs_clear(void) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (state.notice.ancs) memset(&state.notice, 0, sizeof state.notice);
    xSemaphoreGive(mutex);
}
bool terminal_board_overlay_changed(void) {
    /* UI task owns previous; BLE callbacks only mutate state under the mutex. */
    static terminal_notice previous;
    static uint64_t previous_route;
    static bool previous_fresh, previous_route_visible;
    terminal_notice current = {0};
    xSemaphoreTake(mutex, portMAX_DELAY);
    int64_t now = esp_timer_get_time()/1000;
    if (now < state.notice.until && !(state.pin_visible && now < state.pin_until)) current = state.notice;
    bool route_visible=navigation_active && !(state.pin_visible && now<state.pin_until) && !terminal_video_visible();
    bool fresh=state.hud_valid && now<state.hud_until;
    bool navigation_changed=route_visible!=previous_route_visible || (route_visible && (previous_route!=hud_generation || previous_fresh!=fresh));
    previous_route=hud_generation;previous_fresh=fresh;previous_route_visible=route_visible;
    xSemaphoreGive(mutex);
    bool changed = terminal_map_changed() || navigation_changed || memcmp(&previous, &current, sizeof current) != 0;
    previous = current;
    return changed;
}
void terminal_board_overlay(uint16_t *pixels, unsigned width, unsigned height) {
    terminal_notice copy;
    moto_hud draw_hud;
    xSemaphoreTake(mutex, portMAX_DELAY);
    int64_t now = esp_timer_get_time()/1000;
    if (state.notice.until && now >= state.notice.until) memset(&state.notice, 0, sizeof state.notice);
    copy = state.notice;
    bool visible = now < copy.until && !(state.pin_visible && now < state.pin_until);
    bool draw=navigation_active && !(state.pin_visible && now<state.pin_until) && !terminal_video_visible();
    draw_hud=state.hud;
    bool fresh=state.hud_valid && now<state.hud_until, night=state.night;
    xSemaphoreGive(mutex);
    if(draw){
        bool position_fresh=false,complete=false;
        if(terminal_map_draw(pixels,width,height,&position_fresh,&complete))
            terminal_hud_map_overlay(pixels,width,height,&draw_hud,fresh,night,position_fresh,complete);
        else terminal_hud_draw(pixels,width,height,&draw_hud,fresh,night);
    }
    if (visible) terminal_notice_draw(pixels, width, height, &copy);
}
