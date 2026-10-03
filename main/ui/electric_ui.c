/* SPDX-License-Identifier: MIT */
#include "electric_ui.h"
#include <stdio.h>
#include <string.h>
static lv_obj_t *screen, *speed, *battery, *range, *distance, *connection, *style_name;
static electric_style_t style;
static bool night;
static void set_text(lv_obj_t *label, const char *text) {
    if (strcmp(lv_label_get_text(label), text)) lv_label_set_text(label, text);
}
static lv_obj_t *label(int x, int y, const lv_font_t *font) {
    lv_obj_t *v = lv_label_create(screen);
    lv_obj_set_pos(v, x, y);
    lv_obj_set_style_text_font(v, font, 0);
    lv_label_set_text(v, "");
    return v;
}
electric_style_t electric_ui_style(void) { return style; }
void electric_ui_set_night(bool value) {
    night = value;
    if (!screen) return;
    lv_obj_set_style_bg_color(screen, lv_color_hex(night ? 0x10202c : 0xf0f4f7), 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(night ? 0xe5edf3 : 0x152a3b), 0);
}
void electric_ui_set_style(electric_style_t value) {
    if (value != ELECTRIC_STYLE_CLASSIC && value != ELECTRIC_STYLE_COCKPIT) return;
    style = value;
    if (!screen) return;
    set_text(style_name, value == ELECTRIC_STYLE_COCKPIT ? "COCKPIT / OPEN REFERENCE" : "CLASSIC / OPEN REFERENCE");
    lv_obj_set_pos(speed, value == ELECTRIC_STYLE_COCKPIT ? 350 : 400, 195);
    lv_obj_invalidate(screen);
}
void electric_ui_set_state(const electric_state_t *s) {
    if (!screen || !s) return;
    char text[96];
    snprintf(text, sizeof text, "%u km/h", s->speed_kph); set_text(speed, text);
    snprintf(text, sizeof text, "Battery  %u%%", s->battery_percent); set_text(battery, text);
    snprintf(text, sizeof text, "Range  %u km", s->range_km); set_text(range, text);
    snprintf(text, sizeof text, "Trip  %lu.%lu km    Odometer  %lu km",
        (unsigned long)(s->trip_tenths / 10), (unsigned long)(s->trip_tenths % 10),
        (unsigned long)s->odometer_km); set_text(distance, text);
    set_text(connection, s->connected ? "APP CONNECTED   |   VEHICLE DATA: DEMO" : "APP OFFLINE   |   VEHICLE DATA: DEMO");
}
void electric_ui_create(lv_obj_t *parent) {
    screen = lv_obj_create(parent); lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, 1080, 608);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    style_name = label(55, 110, &lv_font_montserrat_24);
    speed = label(400, 195, &lv_font_montserrat_48);
    battery = label(55, 300, &lv_font_montserrat_24);
    range = label(55, 345, &lv_font_montserrat_24);
    distance = label(55, 440, &lv_font_montserrat_20);
    connection = label(55, 495, &lv_font_montserrat_20);
    electric_ui_set_night(false); electric_ui_set_style(ELECTRIC_STYLE_CLASSIC);
    electric_state_t initial = electric_demo_sample(0); electric_ui_set_state(&initial);
}
electric_state_t electric_demo_sample(uint32_t ms) {
    /* A 60 s ride: 10 s parked, 10 s acceleration, 20 s cruise,
       10 s deceleration, 10 s parked. Integral = 0.4 km per cycle. */
    uint32_t t=ms%60000, cycles=ms/60000;
    uint16_t speed=t<10000?0:t<20000?(t-10000)*48/10000:t<40000?48:t<50000?(50000-t)*48/10000:0;
    uint64_t distance=0;
    if(t>=50000) distance=1440000000ULL;
    else if(t>=40000) {uint64_t d=t-40000;distance=1200000000ULL+48000*d-24*d*d/10;}
    else if(t>=20000) distance=240000000ULL+48000ULL*(t-20000);
    else if(t>=10000) {uint64_t d=t-10000;distance=24*d*d/10;}
    uint32_t meters=cycles*400+(uint32_t)(distance/3600000);
    uint32_t used=meters/1200;
    uint8_t battery=used<87?87-used:0;
    return (electric_state_t){.speed_kph=speed,.range_km=battery*120/100,
        .battery_percent=battery,.trip_tenths=meters/100,.odometer_km=1286+meters/1000,
        .hour=(8+ms/3600000)%24,.minute=(ms/60000)%60,.animation_ms=ms};
}
