#include <assert.h>
#include <string.h>
#include "terminal_config.h"
static uint64_t now(void *p) { (void)p; return 1; }
static int random_bytes(void *p, uint8_t *bytes, size_t n) { (void)p; memset(bytes, 1, n); return 0; }
static void event(void *p, const moto_event *e) { (void)p; (void)e; }
static int persist(void *p, bool paired) { (void)p; (void)paired; return 0; }
static int auth(void *p, const moto_pairing_auth *a) { (void)p; (void)a; return 0; }
static int cloud(void *p, const uint8_t *b, size_t n, uint8_t out[32]) { (void)p; (void)b; (void)n; memset(out, 0, 32); return 0; }
static int reset(void *p) { (void)p; return 0; }
static int feed(void *p, const uint8_t *b, size_t n) { (void)p; (void)b; (void)n; return 0; }
static int handshake(void *p) { (void)p; return 0; }
static int read_bytes(void *p, uint8_t *b, size_t n) { (void)p; (void)b; (void)n; return 0; }
static void close_transport(void *p) { (void)p; }
int main(void)
{
    for (unsigned video = 0; video < 2; ++video) {
        moto_config config = {.device_id = "unit-test-only", .now_ms = now, .random = random_bytes,
            .event = event, .persist_pairing = persist, .pairing_pin = true,
            .persist_pairing_auth = auth, .cloud_hmac = cloud,
            .secure = {.reset = reset, .feed = feed, .handshake = handshake,
                       .read = read_bytes, .write = feed, .close = close_transport}};
        terminal_configure_board(&config, video != 0);
        moto_sdk *sdk = moto_create(&config);
        assert(sdk); /* Covers SDK config invariants for real and failed decoder startup. */
        assert(moto_enabled_capabilities(sdk) == 0); /* Local demos cannot grant cloud permissions. */
        assert((config.simulated_capabilities & MOTO_CAP(MOTO_LOCK)) != 0);
        assert((config.simulated_capabilities & MOTO_CAP(MOTO_DISPLAY)) == 0);
        assert(!config.wifi && !config.wifi_ota);
        moto_destroy(sdk);
    }
}
