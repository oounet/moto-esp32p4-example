#pragma once
#include "moto/sdk.h"

/* Board-local demo job; source identifies the stable peer slot, never an SDK pointer
 * dereferenced by the LVGL task. The host checks generation/session on completion. */
typedef struct {
    uint64_t session, request, deadline_ms, generation;
    uint32_t message, value, epoch, source;
    bool success;
} control_effect;
static inline const char *terminal_control_name(uint32_t message) {
    switch (message) {
    case MOTO_LOCK_SET: return "Lock";
    case MOTO_VOLUME_SET: return "Volume";
    case MOTO_LIGHT_SET: return "Light";
    case MOTO_HORN_TRIGGER: return "Horn";
    case MOTO_POWER_SET: return "Power";
    case MOTO_SEAT_OPEN: return "Open seat";
    case MOTO_PROXIMITY_UNLOCK_TRIGGER: return "Proximity unlock";
    default: return NULL;
    }
}
static inline void terminal_control_apply(moto_vehicle_state *state, const control_effect *effect) {
    switch (effect->message) {
    case MOTO_LOCK_SET: state->values[MOTO_LOCK] = effect->value; break;
    case MOTO_VOLUME_SET: state->values[MOTO_VOLUME] = effect->value; break;
    case MOTO_LIGHT_SET: if (effect->value != 2) state->values[MOTO_LIGHT] = effect->value; break;
    case MOTO_POWER_SET: state->values[MOTO_POWER] = effect->value; break;
    case MOTO_PROXIMITY_UNLOCK_TRIGGER: state->values[MOTO_LOCK] = 0; break;
    case MOTO_SEAT_OPEN: break; /* Release only; never synthesize a lid state. */
    default: break;
    }
}
void control_effects_init(void);
bool control_effects_audio_ready(void);
bool control_effects_light_ready(void);
bool control_effects_submit(control_effect effect);
bool control_effects_result(control_effect *effect);
void control_effects_cancel(uint32_t source);
