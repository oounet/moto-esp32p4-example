#include <moto_freertos/terminal.h>
#include <string.h>
/* One instance per physical link. Driver tasks serialize access to this state. */
typedef struct {
    uint64_t connection;
    uint8_t pending[MOTO_TERMINAL_RX_MAX];
    size_t size;
} example_connection_rx;
moto_result example_connection_connected(moto_terminal *t, example_connection_rx *rx) {
    rx->size = 0;
    return moto_terminal_connected(t, &rx->connection);
    /* If not OK, close the new physical link; never overwrite another link's rx. */
}
moto_result example_connection_flush(moto_terminal *t, example_connection_rx *rx) {
    if (!rx->size) return MOTO_OK;
    moto_result rc = moto_terminal_receive(t, rx->connection, rx->pending, rx->size);
    if (rc == MOTO_OK) rx->size = 0; /* FULL retains identical bytes for next tick. */
    return rc;
}
moto_result example_connection_input(moto_terminal *t, example_connection_rx *rx,
                             const uint8_t *bytes, size_t size) {
    if (!bytes || !size || size > sizeof rx->pending) return MOTO_INVALID;
    if (rx->size) return MOTO_FULL; /* New bytes NOT accepted; backpressure the socket. */
    memcpy(rx->pending, bytes, size); rx->size = size;
    moto_result rc = example_connection_flush(t, rx);
    /* Accepted into OUR buffer even if SDK is full: caller must not duplicate. */
    return rc == MOTO_FULL ? MOTO_OK : rc;
}
void example_connection_disconnected(moto_terminal *t, example_connection_rx *rx) {
    moto_result rc = moto_terminal_disconnected(t, rx->connection);
    if (rc == MOTO_OK || rc == MOTO_STATE) { rx->connection = 0; rx->size = 0; }
}
/* Flush on each network task tick. If input returns FULL, retain the new bytes in
 * the transport's bounded RX queue. If that cannot be done, close this link.
 * For SDK STATE/INVALID, stop feeding and close; never skip ahead in TLS bytes. */
