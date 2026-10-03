#include "ota.h"
moto_result example_ota_transport(moto_terminal_config *c, moto_ota_simulator **out) {
    if (!c || !out || *out) return MOTO_INVALID;
    moto_ota_simulator *sim = moto_ota_simulator_create();
    if (!sim) return MOTO_IO;
    c->ota = moto_ota_simulator_port(sim);
    c->wifi_ota = false; /* Start with BLE; enable Wi-Fi only after bulk.c is wired. */
    *out = sim;
    return MOTO_OK;
}
/* After terminal destruction: moto_ota_simulator_destroy(sim); sim = NULL.
 * Production: replace c.ota with a board-owned moto_ota_port, validate signed
 * metadata before BEGIN, acknowledge only durable writes, verify full hash at
 * FINISH, use inactive OTA slot + boot verification/rollback. Do not copy this
 * simulator into a release expecting firmware installation. */
