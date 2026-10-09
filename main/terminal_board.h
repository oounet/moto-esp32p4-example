#pragma once
#include "moto/sdk.h"
#include "control_effects.h"
#include "ui/electric_ui.h"
void p4_terminal_start(void);
bool moto_terminal_ble_ready(void);
bool moto_terminal_wifi_connected(void);
bool moto_terminal_bluetooth_connected(void);
bool moto_terminal_video_ready(void);
bool hud_display_init(void);
void hud_display_set_provisioning(bool required);
void hud_display_event(const moto_event *event);
/* UI task alone owns LVGL, display objects and visible control completions. */
void terminal_board_create(void);
void terminal_board_tick(void);
bool terminal_board_overlay_changed(void);
void terminal_board_overlay(uint16_t *pixels, unsigned width, unsigned height);
electric_state_t terminal_board_state(void);

/* Local preview command queues a built-in style for the LVGL owner task. */
bool terminal_board_select_instrument(const char *id);
const char *terminal_board_instrument(void);

moto_instrument_port terminal_board_instrument_port(void);
void terminal_board_instrument_presented(void);
/* Local display diagnosis: hold the current picture for at most 30 seconds.
 * No persistent state; an active video session cancels the hold. */
void terminal_display_hold(unsigned seconds);
/* 1: hold; 2: render without presentation; 3: swap identical frames. */
void terminal_display_test(unsigned mode);
