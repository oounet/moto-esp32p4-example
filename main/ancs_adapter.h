#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "host/ble_gap.h"
void terminal_ancs_init(void);
void terminal_ancs_connect(uint16_t connection);
void terminal_ancs_disconnect(void);
void terminal_ancs_request(bool enabled);
void terminal_ancs_tick(bool authenticated);
int terminal_ancs_gap(struct ble_gap_event *event, bool authenticated);
bool terminal_ancs_ready(void *user);
bool terminal_ancs_should_disconnect(void);
