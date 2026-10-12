#include <assert.h>
#include <string.h>
#include "terminal_control.h"
int main(void) {
    moto_vehicle_state state = {0};
    state.values[MOTO_LOCK] = 1;
    state.values[MOTO_CHARGING] = 2;
    state.values[MOTO_MILEAGE] = 456;
    state.values[MOTO_RANGE_METRES] = 12000;
    control_effect effect = {.message=MOTO_POWER_SET, .value=1};
    terminal_control_apply(&state, &effect);
    assert(state.values[MOTO_POWER] == 1 && state.values[MOTO_LOCK] == 1);
    effect.value = 0; terminal_control_apply(&state, &effect);
    assert(state.values[MOTO_POWER] == 0 && state.values[MOTO_CHARGING] == 2);
    effect.message = MOTO_PROXIMITY_UNLOCK_TRIGGER; terminal_control_apply(&state, &effect);
    assert(state.values[MOTO_LOCK] == 0 && state.values[MOTO_MILEAGE] == 456 && state.values[MOTO_RANGE_METRES] == 12000);
    assert(terminal_control_name(MOTO_POWER_SET) && terminal_control_name(MOTO_PROXIMITY_UNLOCK_TRIGGER));
    assert(!terminal_control_name(MOTO_CONTROL_RESULT));
    moto_vehicle_state before = state;
    effect.message = MOTO_SEAT_OPEN; effect.value = 1;
    assert(terminal_control_name(effect.message));
    terminal_control_apply(&state, &effect);
    assert(memcmp(&before, &state, sizeof state) == 0);
    return 0;
}
