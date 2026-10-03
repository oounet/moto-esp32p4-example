#include <moto_freertos/terminal.h>
/* Same stable event ID for raise, retries and clear. IDs must not be reused for
 * another incident. Caller supplies confirmed CAN/controller values.
 * FULL: retry the same snapshot; OK: do not resubmit until actual state changes. */
moto_result example_fault(moto_terminal *t, uint64_t incident_id, bool active,
                          const char *controller_namespace, uint32_t code,
                          uint32_t severity) {
    return moto_terminal_report_fault(t, incident_id, active,
                                      controller_namespace, code, severity);
}
