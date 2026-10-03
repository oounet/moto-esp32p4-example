#include "advertising.h"
#include <moto_freertos/terminal.h>
#include <mbedtls/sha256.h>
#include <string.h>

/* Small PNG reference adapter. Caller owns bounded RAM; receipts live in a real
 * durable store. Storage/display functions are supplied by your BSP, not SDK. */

static int example_ad_lookup(void *u, const uint8_t ad[16], uint8_t sha[32], bool *used) {
    example_ad_store *s = u;
    return s->lookup(s->user, ad, sha, used);
}
static int example_ad_begin(void *u, const moto_ad_media *m) {
    example_ad_store *s = u;
    if (m->mime != MOTO_AD_PNG || m->size > s->capacity) return MOTO_UNSUPPORTED;
    s->media = *m; s->offset = 0; s->verified = false;
    s->receiving = mbedtls_sha256_starts(&s->hash, 0) == 0;
    return s->receiving ? MOTO_OK : MOTO_IO;
}
static int example_ad_write(void *u, uint32_t offset, const uint8_t *bytes, size_t size) {
    example_ad_store *s = u;
    if (!s->receiving || offset != s->offset || size > s->media.size - s->offset)
        return MOTO_STATE;
    if (mbedtls_sha256_update(&s->hash, bytes, size)) { s->receiving = false; return MOTO_IO; }
    memcpy(s->buffer + offset, bytes, size); s->offset += (uint32_t)size;
    return MOTO_OK;
}
static int example_ad_finish(void *u, const moto_ad_media *m) {
    example_ad_store *s = u;
    if (!s->receiving || s->offset != m->size) return MOTO_STATE;
    if (!s->verified) {
        /* Finish a COPY: a failed durable commit can retry without destroying hash state. */
        mbedtls_sha256_context copy;
        uint8_t digest[32];
        mbedtls_sha256_init(&copy); mbedtls_sha256_clone(&copy, &s->hash);
        int rc = mbedtls_sha256_finish(&copy, digest); mbedtls_sha256_free(&copy);
        if (rc || memcmp(digest, m->sha256, 32)) return MOTO_IO;
        s->verified = true;
    }
    int rc = s->commit_receipt(s->user, m->ad, m->sha256);
    if (rc == MOTO_OK) s->receiving = false;
    return rc; /* 0 ONLY after atomic durable commit. */
}
static int example_ad_consume(void *u, const uint8_t ad[16]) {
    example_ad_store *s = u;
    return s->consume_once(s->user, ad); /* Commit before SDK calls play. */
}
static int example_ad_play(void *u, const moto_ad_media *m) {
    example_ad_store *s = u;
    if (!s->verified || s->offset != m->size || memcmp(m->ad, s->media.ad, 16))
        return MOTO_STATE;
    return s->show_png(s->user, s->buffer, s->offset, &s->media);
}
static void example_ad_stop(void *u) {
    example_ad_store *s = u;
    s->stop_display(s->user);
    s->receiving = s->verified = false; s->offset = 0;
    mbedtls_sha256_free(&s->hash); mbedtls_sha256_init(&s->hash);
    /* Do not delete receipts or consumed bits. */
}
static bool example_ad_safe(void *u) {
    example_ad_store *s = u;
    return s->safe_idle(s->user);
}
moto_result example_ad_port(example_ad_store *s, uint16_t width, uint16_t height,
                            moto_ad_port *out) {
    if (!s || !out || !s->buffer || !s->capacity || s->capacity > MOTO_AD_MAX_SIZE ||
        !width || !height || !s->lookup || !s->commit_receipt || !s->consume_once ||
        !s->show_png || !s->stop_display || !s->safe_idle) return MOTO_INVALID;
    mbedtls_sha256_init(&s->hash); s->offset = 0; s->receiving = s->verified = false;
    *out = (moto_ad_port){s, s->capacity, 512, MOTO_AD_MIME_MASK(MOTO_AD_PNG), width, height,
        example_ad_lookup, example_ad_begin, example_ad_write, example_ad_finish,
        example_ad_consume, example_ad_play, example_ad_stop, example_ad_safe};
    return MOTO_OK;
}
/* Zero-initialize store once, wire REAL callbacks and buffer, call example_ad_port
 * before create, assign returned port to c.advertising. Keep store/buffer alive
 * through terminal destruction. All callbacks run on SDK task, must be bounded;
 * asynchronous display must COPY the bytes/metadata before returning.
 * This is PNG only, not a Flash filesystem, durable receipt implementation or decoder. */
