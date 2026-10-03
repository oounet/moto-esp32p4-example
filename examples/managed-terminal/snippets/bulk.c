#include <moto_freertos/terminal.h>
/* The listener owns an immutable copy, not a pointer into the UI event loop. */
typedef struct { moto_terminal_event wifi; uint64_t connection; } example_bulk;
moto_result example_bulk_listening(moto_terminal *t, example_bulk *b,
                                   const moto_terminal_event *e,
                                   const moto_terminal_hotspot *actual) {
    if (b->connection) return MOTO_BUSY;
    b->wifi = *e;
    return moto_terminal_bulk_ready(t, &b->wifi, actual);
    /* FULL: retain/retry. AP and TCP listener must already be usable. */
}
moto_result example_bulk_accept(moto_terminal *t, example_bulk *b) {
    uint64_t id = 0;
    moto_result rc = moto_terminal_bulk_connected(t, &b->wifi, &id);
    if (rc == MOTO_OK) b->connection = id;
    return rc; /* Any failure: close THIS newly accepted socket, retain existing mapping. */
}
moto_result example_bulk_input(moto_terminal *t, example_bulk *b,
                               const uint8_t *bytes, size_t size) {
    return moto_terminal_bulk_receive(t, b->connection, bytes, size);
    /* Up to 512 bytes. FULL: retain exact bytes, pause socket reads, retry on tick. */
}
void example_bulk_closed(moto_terminal *t, example_bulk *b, uint64_t closed_id) {
    (void)moto_terminal_bulk_disconnected(t, closed_id);
    if (b->connection == closed_id) b->connection = 0;
}
/* Wire config.bulk.send/close to ordered nonblocking TCP TX and idempotent close;
 * callbacks receive the generation to map to a socket. Never log keys. The SDK
 * owns PSK/TLS/polling. Stop listeners and producers before terminal destroy. */
