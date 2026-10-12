#include <moto_freertos/terminal.h>
/* Core BLE callback: out owns strings, so the UI can queue a value copy.
 * The caller also saves its own connection/session and receipt time. */
moto_result example_core_notice(const moto_event *event, moto_notification *out) {
    return moto_event_get_notification(event, out);
}
/* UI task owns this slot; a real list uses bounded slots keyed by notification ID. */
typedef struct { bool visible; moto_terminal_event saved; } example_notice;
void example_notice_event(example_notice *ui, const moto_terminal_event *e) {
    if (e->kind == MOTO_EVENT_NOTIFICATION) { ui->saved = *e; ui->visible = true; }
    if (e->kind == MOTO_EVENT_CLOSED ||
        (e->kind == MOTO_EVENT_NOTIFICATION_REMOVE &&
         e->data.object.id == ui->saved.data.notification.id)) ui->visible = false;
}
moto_result example_notice_click(moto_terminal *t, example_notice *ui, uint32_t action,
                                 uint64_t now_ms) {
    if (!ui->visible || now_ms >= ui->saved.deadline_ms) return MOTO_EXPIRED;
    if (action > 1) return MOTO_INVALID;
    if (!(ui->saved.data.notification.actions & (1u << action))) return MOTO_UNSUPPORTED;
    return moto_terminal_notification_action(t, &ui->saved, action);
    /* FULL: retry same click; OK: await ACTION_RESULT. Do not duplicate OK. */
}
moto_result example_hfp_result(moto_terminal *t, const moto_terminal_event *saved,
                              bool driver_confirmed, uint32_t reason) {
    return moto_terminal_complete(t, saved,
        driver_confirmed ? MOTO_SUCCEEDED : MOTO_FAILED, reason);
}
moto_result example_hfp_state(moto_terminal *t, uint64_t call_id,
                             uint32_t state, uint32_t available_actions) {
    return moto_terminal_report_call(t, call_id, state, available_actions);
}
/* c.notifications enables protocol input, not BLE. The current APP sends
 * notifications to a separate Core BLE companion; Wi-Fi alone supplies no input.
 * The managed notice helpers require an already-wired notification transport.
 * Enable c.calls only after HFP integration.
 * CALL_ACTION: copy event to the HFP task, operate once, retry only completion.
 * ANCS: c.ancs_ready(port.user) must read the actual encrypted GATT subscription
 * state under the BSP's lock. ANCS_SHARING expresses intent, not permission. */
