#include "media_session.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    const uint8_t legacy[] = {0xa1, 0x00, 0x09};
    const uint8_t navigation[] = {0xa2, 0x00, 0x09, 0x01, 0x00};
    const uint8_t mirror[] = {0xa2, 0x00, 0x09, 0x01, 0x01};
    assert(terminal_session_has_navigation_hud((moto_slice){legacy, sizeof legacy}));
    assert(terminal_session_has_navigation_hud((moto_slice){navigation, sizeof navigation}));
    assert(!terminal_session_has_navigation_hud((moto_slice){mirror, sizeof mirror}));
    puts("Navigation HUD and mirror session separation passed");
    return 0;
}
