#include <moto_freertos/terminal.h>
#include <stdio.h>
moto_result example_diagnose_create(const moto_terminal_config *c, moto_terminal **out) {
    moto_terminal_error e;
    moto_result rc = moto_terminal_create(c, out, &e);
    if (rc != MOTO_OK)
        printf("create: stage=%s code=%s detail=%s\n",
               moto_terminal_stage_name(e.stage), moto_terminal_result_name(e.code), e.detail);
    return rc;
}
void example_diagnose_runtime(moto_terminal *t) {
    moto_terminal_status s = moto_terminal_get_status(t);
    printf("running=%d authenticated=%d stage=%s code=%s\n", s.running, s.authenticated,
           moto_terminal_stage_name(s.error.stage), moto_terminal_result_name(s.error.code));
}
/* On your video task also record receive/decode/LCD-complete COUNTERS separately.
 * Never log PIN, License, PSK, owner token or hotspot password. */
