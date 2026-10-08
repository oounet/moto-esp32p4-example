#pragma once
#include "moto/sdk.h"
moto_map_port terminal_map_port(void);
void terminal_map_event(const moto_event *);
bool terminal_map_changed(void);
bool terminal_map_draw(uint16_t *,unsigned,unsigned,bool *fresh,bool *complete);
void terminal_map_poll(moto_sdk *);
