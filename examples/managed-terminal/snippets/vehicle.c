#include <moto_freertos/terminal.h>
/* Input values come from a real CAN/UART snapshot. Called on the reporting task.
 * FULL: retain the same snapshot and retry; OK: wait for normal next sample. */
moto_result example_vehicle_sample(moto_terminal *t, unsigned soc_percent,
                                   uint32_t total_metres) {
    if (soc_percent > 100) return MOTO_INVALID;
    moto_vehicle_state state = {0};
    state.present = MOTO_CAP(MOTO_BATTERY) | MOTO_CAP(MOTO_MILEAGE);
    state.values[MOTO_BATTERY] = soc_percent;
    state.values[MOTO_MILEAGE] = total_metres;
    return moto_terminal_report_state(t, &state);
}
/* Call BEFORE placing a copied VEHICLE_COMMAND on the actuator queue, and again
 * at actual execution. now_ms is your thread-safe monotonic clock. */
bool example_vehicle_command_current(moto_terminal *t, const moto_terminal_event *e,
                                     uint64_t now_ms) {
    moto_terminal_status s = moto_terminal_get_status(t);
    return e && e->kind == MOTO_EVENT_VEHICLE_COMMAND && s.running &&
           s.authenticated && s.connection == e->connection && s.session == e->session &&
           e->deadline_ms && now_ms < e->deadline_ms;
}
/* Real hardware result -> example_completion in api.c -> example_complete_tick.
 * Do not acknowledge the CAN TX enqueue as hardware success. Fault IDs remain
 * stable for retries and for the later active=false report. */
