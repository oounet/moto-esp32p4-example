#include <moto_freertos/terminal.h>
/* Bind these functions to your actual factory store / atomic key-value store. */
typedef struct {
    void *user;
    int (*factory_read)(void *, uint8_t license[512], char hw[65], uint8_t key[65]);
    /* 0 found, 1 absent, negative error. Never treat corruption as absent. */
    int (*record_read)(void *, uint8_t *, size_t capacity, size_t *size);
    int (*record_commit)(void *, const uint8_t *, size_t); /* 0 after durable commit */
    int (*secure_random)(void *, uint8_t *, size_t);
    uint64_t (*monotonic_ms)(void *);
} example_store;
int example_license(void *u, uint8_t bytes[512], char hw[65], uint8_t key[65]) {
    example_store *s = u;
    return s->factory_read(s->user, bytes, hw, key);
}
int example_pairing_read(void *u, uint8_t *out, size_t capacity, size_t *size) {
    example_store *s = u;
    int rc = s->record_read(s->user, out, capacity, size);
    if (rc == 1) { *size = 0; return 0; }
    return rc;
}
int example_pairing_write(void *u, const uint8_t *bytes, size_t size) {
    example_store *s = u;
    return s->record_commit(s->user, bytes, size);
}
int example_random(void *u, uint8_t *out, size_t size) {
    example_store *s = u;
    return s->secure_random(s->user, out, size);
}
uint64_t example_now(void *u) {
    example_store *s = u;
    return s->monotonic_ms(s->user); /* Must also be safe from event-consumer task. */
}
/* Assign port.user=&store and the five callbacks above before create.
 * BLE send/close must use the same port.user or wrap a common board context. */
