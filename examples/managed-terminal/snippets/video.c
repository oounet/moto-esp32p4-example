#include <moto/video.h>
#include <moto_freertos/terminal.h>
typedef struct {
    moto_terminal *terminal;
    moto_video *video;
    uint64_t session, peer;
    uint32_t stream;
} example_video;
/* Driver frame callback must consume/copy compressed data before returning.
 * It runs on the video task, never on the SDK control task. */
moto_result example_video_start(example_video *v, moto_terminal *t,
                               const moto_terminal_event *start, uint64_t verified_peer,
                               const moto_video_config *decoder) {
    if (!v || !start || start->kind != MOTO_EVENT_STREAM_START || !verified_peer)
        return MOTO_INVALID;
    if (v->video) return MOTO_BUSY; /* Stop and destroy the preceding stream first. */
    moto_terminal_status status = moto_terminal_get_status(t);
    if (status.session != start->session || status.connection != start->connection)
        return MOTO_STATE;
    v->video = moto_video_create(decoder);
    if (!v->video) return MOTO_IO;
    v->terminal = t; v->session = start->session; v->peer = verified_peer;
    v->stream = start->data.stream.stream_id;
    moto_result rc = moto_video_begin_codec(v->video, v->peer, v->stream,
        start->data.stream.width, start->data.stream.height, start->data.stream.codec);
    if (rc != MOTO_OK) { moto_video_destroy(v->video); v->video = NULL; }
    return rc;
}
moto_result example_video_datagram(example_video *v, uint64_t checked_source,
                                   const uint8_t *bytes, size_t size, uint64_t now_ms) {
    if (!v->video || checked_source != v->peer) return MOTO_STATE;
    return moto_video_receive(v->video, checked_source, bytes, size, now_ms);
}
void example_video_idle_tick(example_video *v, uint64_t now_ms) {
    if (v->video) moto_video_tick(v->video, now_ms); /* Every 10–20 ms even with no UDP. */
}
moto_result example_video_displayed(example_video *v, uint64_t frame_id) {
    return moto_terminal_presented(v->terminal, v->session, v->stream, frame_id);
}
void example_video_stop(example_video *v) {
    if (v->video) { moto_video_stop(v->video); moto_video_destroy(v->video); }
    v->video = NULL;
}
/* STREAM_STOP/NAV_END/CLOSED -> stop RX, cancel/join decoder/LCD jobs, then stop.
 * Serialize all v access on video task. LCD callbacks queue captured IDs back to
 * that task, never reread IDs from a newer stream. Decoder failures drop the frame;
 * only actual LCD completion calls displayed. Never reenter video from callbacks. */
