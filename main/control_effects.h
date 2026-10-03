#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    uint64_t session, request, deadline_ms;
    uint32_t message, value, epoch;
    bool success;
} control_effect;
/* All public calls run on the host task (init before host starts). Worker never calls SDK. */
void control_effects_init(void);
bool control_effects_audio_ready(void);
bool control_effects_light_ready(void);
bool control_effects_submit(control_effect effect);
bool control_effects_result(control_effect *effect);
void control_effects_cancel(void);
