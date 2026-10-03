#include <moto_freertos/terminal.h>
/* A hardware operation is executed ONCE; only its result is retried. */
typedef struct {
    bool pending;
    moto_terminal_event event;
    enum moto_execution result;
    uint32_t reason;
} example_completion;
moto_result example_complete_tick(moto_terminal *t, example_completion *work) {
    if (!work->pending) return MOTO_AGAIN;
    moto_result rc = moto_terminal_complete(t, &work->event, work->result, work->reason);
    if (rc != MOTO_FULL) work->pending = false;
    return rc; /* FULL: retry same work next tick, never execute hardware again. */
}
/* Invoke after stopping BLE/TCP producers, UI readers and video tasks. */
moto_result example_shutdown_tick(moto_terminal **t) {
    if (!t || !*t) return MOTO_INVALID;
    moto_result rc = moto_terminal_stop(*t, 0);
    if (rc != MOTO_OK) return rc; /* AGAIN: next task tick; handle remains valid. */
    rc = moto_terminal_destroy(*t);
    if (rc == MOTO_OK) *t = NULL;
    return rc;
}
