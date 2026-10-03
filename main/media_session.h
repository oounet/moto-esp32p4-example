#pragma once
#include "moto/cbor.h"

/* NAV_BEGIN is shared by navigation and screen mirroring. Only navigation may
 * fall back to semantic HUD; a mirror has no navigation instructions. The SDK
 * has already validated this body. Legacy senders omit source (key 1). */
static inline bool terminal_session_has_navigation_hud(moto_slice body) {
    moto_slice field;
    uint64_t source = 0;
    if (moto_cbor_find(body, 1, &field) && !moto_cbor_get_uint(field, &source)) return false;
    return source == 0;
}
