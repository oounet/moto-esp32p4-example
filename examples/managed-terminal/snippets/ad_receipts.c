#include "ad_receipts.h"
#include <moto_freertos/terminal.h>
#include <stdio.h>
#include <string.h>
static void key(unsigned i, char out[8]) { snprintf(out, 8, "r%02u", i); }
void example_receipts_close(example_ad_receipts *s) {
    if (s && s->open) { nvs_close(s->handle); s->open = false; }
}
int example_receipts_open(example_ad_receipts *s) {
    if (!s || s->open) return MOTO_INVALID; /* caller zero-initializes once */
    memset(s, 0, sizeof *s);
    if (nvs_open("moto_ad_rcpt", NVS_READWRITE, &s->handle) != ESP_OK) return MOTO_IO;
    s->open = true;
    for (unsigned i = 0; i < EXAMPLE_AD_RECEIPTS; ++i) {
        char name[8]; key(i, name); size_t size = sizeof s->row[i];
        esp_err_t rc = nvs_get_blob(s->handle, name, s->row[i], &size);
        if (rc == ESP_ERR_NVS_NOT_FOUND) continue;
        if (rc != ESP_OK || size != sizeof s->row[i] || s->row[i][48] > 1) {
            example_receipts_close(s); return MOTO_IO;
        }
        s->present[i] = true;
        for (unsigned j = 0; j < i; ++j)
            if (s->present[j] && !memcmp(s->row[j], s->row[i], 16)) {
                example_receipts_close(s); return MOTO_IO;
            }
    }
    return MOTO_OK;
}
static int find(example_ad_receipts *s, const uint8_t ad[16]) {
    for (unsigned i = 0; i < EXAMPLE_AD_RECEIPTS; ++i)
        if (s->present[i] && !memcmp(s->row[i], ad, 16)) return (int)i;
    return -1;
}
int example_receipts_lookup(void *u, const uint8_t ad[16], uint8_t sha[32], bool *used) {
    example_ad_receipts *s = u;
    if (!s || !s->open || s->failed || !ad || !sha || !used) return MOTO_IO;
    int i = find(s, ad); if (i < 0) return 0;
    memcpy(sha, s->row[i] + 16, 32); *used = s->row[i][48] != 0; return 1;
}
static int save(example_ad_receipts *s, unsigned i, const uint8_t row[49]) {
    char name[8]; key(i, name);
    if (nvs_set_blob(s->handle, name, row, 49) != ESP_OK || nvs_commit(s->handle) != ESP_OK) {
        /* Outcome may be ambiguous: stop all playback until close/reopen reads NVS. */
        s->failed = true; return MOTO_IO;
    }
    memcpy(s->row[i], row, 49); s->present[i] = true; return MOTO_OK;
}
int example_receipts_commit(void *u, const uint8_t ad[16], const uint8_t sha[32]) {
    example_ad_receipts *s = u;
    if (!s || !s->open || s->failed || !ad || !sha) return MOTO_IO;
    int i = find(s, ad);
    if (i >= 0) return memcmp(s->row[i] + 16, sha, 32) ? MOTO_STATE : MOTO_OK;
    for (unsigned j = 0; j < EXAMPLE_AD_RECEIPTS; ++j) if (!s->present[j]) {
        uint8_t row[49] = {0}; memcpy(row, ad, 16); memcpy(row + 16, sha, 32);
        return save(s, j, row);
    }
    return MOTO_IO; /* No eviction: forgetting a receipt would permit replay. */
}
int example_receipts_consume(void *u, const uint8_t ad[16]) {
    example_ad_receipts *s = u;
    if (!s || !s->open || s->failed || !ad) return MOTO_IO;
    int i = find(s, ad); if (i < 0) return MOTO_STATE;
    if (s->row[i][48]) return 1;
    uint8_t row[49]; memcpy(row, s->row[i], sizeof row); row[48] = 1;
    return save(s, (unsigned)i, row); /* Durable BEFORE play, including power loss. */
}
