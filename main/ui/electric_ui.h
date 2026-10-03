#pragma once
#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    uint16_t speed_kph, range_km;
    uint8_t battery_percent;
    uint32_t trip_tenths, odometer_km;
    bool connected, charging;
    uint8_t hour, minute;
    uint32_t animation_ms;
} electric_state_t;
typedef enum { ELECTRIC_STYLE_CLASSIC = 0, ELECTRIC_STYLE_COCKPIT = 1 } electric_style_t;
/* Single instance, 1080x608. Only the LVGL owner task may call UI functions. */
void electric_ui_create(lv_obj_t *parent);
void electric_ui_set_night(bool night);
void electric_ui_set_style(electric_style_t style);
electric_style_t electric_ui_style(void);
void electric_ui_set_state(const electric_state_t *state);
/* Explicit demo values, never derived from fuel/engine telemetry. */
electric_state_t electric_demo_sample(uint32_t elapsed_ms);
