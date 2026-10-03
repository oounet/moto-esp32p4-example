#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <nvs.h>
#define EXAMPLE_AD_RECEIPTS 64
/* One SDK task owns all calls. Initialize before create; close after destroy.
 * Never erase this namespace on unbind. Full/corrupt storage fails closed. */
typedef struct {
    nvs_handle_t handle;
    bool open, failed, present[EXAMPLE_AD_RECEIPTS];
    uint8_t row[EXAMPLE_AD_RECEIPTS][49]; /* UUID(16), SHA256(32), consumed(1) */
} example_ad_receipts;
int example_receipts_open(example_ad_receipts *);
void example_receipts_close(example_ad_receipts *);
int example_receipts_lookup(void *, const uint8_t[16], uint8_t[32], bool *);
int example_receipts_commit(void *, const uint8_t[16], const uint8_t[32]);
int example_receipts_consume(void *, const uint8_t[16]);
