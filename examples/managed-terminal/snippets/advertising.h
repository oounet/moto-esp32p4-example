#pragma once
#include <moto_freertos/terminal.h>
#include <mbedtls/sha256.h>
typedef struct {
    void *user;
    uint8_t *buffer;
    uint32_t capacity, offset;
    moto_ad_media media;
    mbedtls_sha256_context hash;
    bool receiving, verified;
    int (*lookup)(void *, const uint8_t ad[16], uint8_t sha[32], bool *consumed);
    int (*commit_receipt)(void *, const uint8_t ad[16], const uint8_t sha[32]);
    int (*consume_once)(void *, const uint8_t ad[16]); /* atomic: 0 new, 1 already */
    int (*show_png)(void *, const uint8_t *, size_t, const moto_ad_media *);
    void (*stop_display)(void *); /* Cancel display reads before RAM reuse. */
    bool (*safe_idle)(void *);
} example_ad_store;
moto_result example_ad_port(example_ad_store *, uint16_t, uint16_t, moto_ad_port *);
