/* Public customer glue template, NOT the private SDK implementation.
 * Replace these functions with the actual BSP. Unimplemented operations fail. */
#include "board_driver.h"
#include <moto/network_socket.h>
#include <stdio.h>
static moto_terminal *terminal;
static int load_license(void *user, uint8_t bytes[512], char hardware[65], uint8_t key[65]) {
    (void)user;
    (void)bytes;
    (void)hardware;
    (void)key;
    /* 1. Read this device's 512-byte .motolic from provisioned storage.
     * 2. Read the stable hardware ID used when issuing that License.
     * 3. Copy the trusted 65-byte P-256 public key from firmware configuration.
     * Return 0 only after all three outputs are filled. Never ship a private key. */
    return -1;
}
static moto_network_link network_link(void *user) {
    (void)user;
    /* Read a thread-safe BSP snapshot. ready means interface/IP usable, not
     * Internet access. Increment generation on loss/replacement, even when a
     * quick down/up reuses the same IP. In AP mode also report client loss. */
    return (moto_network_link){false, 0};
}
moto_network_port board_network_port(void) {
    /* This sample address belongs to BSP configuration, not the SDK policy.
     * Keep the configuration alive until SDK is destroyed. For a non-BSD stack,
     * implement moto_network_port primitives instead of this optional backend. */
    static moto_network_socket_config config={.bind_ipv4="192.168.4.1",.link=network_link};
    return moto_network_socket_port(&config);
}
void board_fill_port(moto_terminal_port *port) {
    *port = (moto_terminal_port){.load_license = load_license};
    /* ESP-IDF RNG, clock and atomic NVS owner storage are supplied by SDK. */
}
int board_start_connection(moto_terminal *value) {
    terminal = value;
    /* Start persistent AP and IP/DHCP. SDK owns discovery, TCP listeners,
     * receive ordering, retry/backpressure and stale connection cleanup.
     * Return success only when the network is usable. BLE notifications use a
     * separate auxiliary core instance, as shown in the full P4 reference. */
    puts("board_start_connection: BSP not implemented");
    return -1;
}
void board_stop_connection(void) { /* Stop BSP callbacks and make network_link not ready. */
    terminal = NULL;
}
void board_show_pin(uint32_t pin, uint32_t seconds) {
    (void)pin;
    (void)seconds; /* Show four digits on LCD; never log PIN. */
}
void board_clear_pin(void) { /* Clear local PIN overlay. */ }
void board_theme(enum moto_theme theme) {
    (void)theme; /* Switch actual LCD/LVGL theme. */
}
int board_start_hotspot(moto_terminal_hotspot *actual) {
    (void)actual;
    return -1;
}
void board_stop_hotspot(
    void) { /* Apply product policy; do not rotate SSID/password on navigation. */ }
int board_prepare_video(moto_terminal *t, const moto_terminal_event *e) {
    (void)t;
    (void)e;
    return -1;
}
void board_start_video(const moto_terminal_event *e) {
    (void)e; /* Release prepared video task. */
}
void board_stop_video(void) { /* Stop RX/decode; cancel outstanding display buffers safely. */ }

/* Optional advertising port. These fail-closed stubs are NOT a media player. */
static int ad_lookup(void *u, const uint8_t ad[16], uint8_t hash[32], bool *used) {
    (void)u; (void)ad; (void)hash; (void)used;
    /* Read durable receipt: 0 absent, 1 received with hash/consumed filled.
     * Return IO on read failure; never treat corruption as absence. */
    return MOTO_UNSUPPORTED;
}
static int ad_begin(void *u, const moto_ad_media *media) {
    (void)u; (void)media; /* Reserve bounded storage, copy metadata. */
    return MOTO_UNSUPPORTED;
}
static int ad_write(void *u, uint32_t offset, const uint8_t *data, size_t size) {
    (void)u; (void)offset; (void)data; (void)size;
    /* Accept bytes and update incremental SHA-256 before returning OK. */
    return MOTO_UNSUPPORTED;
}
static int ad_finish(void *u, const moto_ad_media *media) {
    (void)u; (void)media; /* Verify length/hash, atomically commit ad ID + hash. */
    return MOTO_UNSUPPORTED;
}
static int ad_consume(void *u, const uint8_t ad[16]) {
    (void)u; (void)ad; /* Durable commit FIRST: 0 newly consumed, 1 already consumed. */
    return MOTO_UNSUPPORTED;
}
static int ad_play(void *u, const moto_ad_media *media) {
    (void)u; (void)media; /* Start one presentation; copy any retained metadata. */
    return MOTO_UNSUPPORTED;
}
static void ad_stop(void *u) {
    (void)u; /* Cancel incomplete reception/playback; preserve durable receipts. */
}
static bool ad_safe_idle(void *u) {
    (void)u; /* Check actual vehicle/display state. Default refuses playback. */
    return false;
}
moto_ad_port board_advertising_port(void) {
    /* Fill REAL limits after implementing above. Zero limits fail configuration. */
    return (moto_ad_port){.lookup = ad_lookup, .begin = ad_begin, .write = ad_write,
        .finish = ad_finish, .consume = ad_consume, .play = ad_play,
        .stop = ad_stop, .safe_idle = ad_safe_idle};
}
static int tcp_send(void *u, uint64_t id, const uint8_t *data, size_t size) {
    (void)u; (void)id; (void)data; (void)size;
    /* Copy accepted bytes to bounded TX queue for this TCP generation.
     * Return accepted count, 0 backpressure, negative failure. Never block. */
    return -1;
}
static void tcp_close(void *u, uint64_t id) {
    (void)u; (void)id; /* Request idempotent asynchronous close of EXACT generation. */
}
moto_terminal_bulk_port board_bulk_port(void) {
    return (moto_terminal_bulk_port){.send = tcp_send, .close = tcp_close};
}
int board_start_bulk_hotspot(const moto_terminal_event *e, moto_terminal_hotspot *actual) {
    (void)e; (void)actual;
    /* Start WPA2 AP, IP/DHCP and TCP listener. Save e BY VALUE in listener context.
     * On accept call board_on_tcp_connected with that saved event. Only accept
     * bytes after OK; STATE means readiness not processed/stale, close socket.
     * No PSK or TLS work here: SDK owns both. Stop listeners in board_stop_hotspot. */
    return -1;
}
moto_result board_on_tcp_connected(const moto_terminal_event *saved, uint64_t *id) {
    return moto_terminal_bulk_connected(terminal, saved, id);
}
moto_result board_on_tcp_rx(uint64_t id, const uint8_t *data, size_t size) {
    /* Split in order to <=512 bytes. FULL accepts nothing: retain/retry unchanged. */
    return moto_terminal_bulk_receive(terminal, id, data, size);
}
void board_on_tcp_disconnected(uint64_t id) {
    (void)moto_terminal_bulk_disconnected(terminal, id);
}
