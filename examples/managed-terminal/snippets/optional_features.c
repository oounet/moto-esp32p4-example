#include <moto_freertos/terminal.h>
#include <string.h>

/* No protocol job required. Call before create, only for implemented features. */
void example_enable_optional(moto_terminal_config *c, bool routes, bool music,
                             const moto_instrument_port *real_instrument) {
    c->route_navigation = routes;
    c->media_remote = music;
    if (real_instrument) c->instrument = *real_instrument;
}
moto_result example_music_button(moto_terminal *t, const moto_terminal_event *saved,
                                 uint32_t action, uint64_t now_ms) {
    if (!saved || saved->kind != MOTO_EVENT_MEDIA) return MOTO_INVALID;
    if (!saved->deadline_ms || now_ms >= saved->deadline_ms) return MOTO_EXPIRED;
    return moto_terminal_media_action(t, saved, action);
    /* 0 play, 1 pause, 2 previous, 3 next. FULL: retry; OK: await result/state. */
}
/* Keep this state on the UI task. Draw only matching route/position revisions. */
typedef struct { moto_route route; moto_position position; bool positioned; } example_route_ui;
void example_route_event(example_route_ui *ui, const moto_terminal_event *e) {
    switch (e->kind) {
    case MOTO_EVENT_ROUTE:
        ui->route = e->data.route; ui->positioned = false; break;
    case MOTO_EVENT_POSITION:
        if (e->data.position.id == ui->route.id &&
            e->data.position.route_revision == ui->route.revision) {
            ui->position = e->data.position; ui->positioned = true;
        }
        break;
    case MOTO_EVENT_POSITION_EXPIRED: ui->positioned = false; break;
    case MOTO_EVENT_NAV_END:
    case MOTO_EVENT_CLOSED: memset(ui, 0, sizeof *ui); break;
    default: break;
    }
}
/* UI queueing needs an additional deadline/session check at execution. */
