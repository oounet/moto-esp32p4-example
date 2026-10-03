#ifndef MOTO_HUD_DISPLAY_H
#define MOTO_HUD_DISPLAY_H
#include "moto/ancs.h"
#include "moto/sdk.h"
/* Public ANCS/display adapter implemented by terminal_board.c. */
bool hud_display_init(void);
/* Boot owner before NimBLE starts, then the serialized NimBLE owner after unpair. */
void hud_display_set_provisioning(bool required);
/* Copies borrowed SDK data into a one-element latest-state queue; never performs SPI here. */
void hud_display_event(const moto_event *event);
/* Same NimBLE host-task ownership as hud_display_event. */
void hud_display_ancs(const moto_ancs_notification *);
void hud_display_ancs_remove(uint32_t uid);
void hud_display_ancs_clear(void);
#endif
