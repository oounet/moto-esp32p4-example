#pragma once
#include <moto_freertos/terminal.h>
#include <moto/ota_simulator.h>
/* Call before terminal create; keep simulator through terminal destroy.
 * Development only: never writes firmware, reboots or advertises real installation. */
moto_result example_ota_transport(moto_terminal_config *, moto_ota_simulator **);
