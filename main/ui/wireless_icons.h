#ifndef MOTO_WIRELESS_ICONS_H
#define MOTO_WIRELESS_ICONS_H
#include <stdbool.h>
#include <stdint.h>

/* Shared 24 px geometry; every renderer uses a 2 px stroke. */
typedef void (*wireless_line_fn)(void *, int, int, int, int, uint32_t);
static inline uint32_t wireless_color(bool connected, bool night) {
    return connected ? 0x21d4cb : (night ? 0xa8adb8 : 0x45557b);
}
static inline void wireless_icon(void *ctx, wireless_line_fn line, int x, int y,
                                 bool bluetooth, bool connected, bool night) {
    static const int8_t wifi[][4] = {
        {2,8,6,5},{6,5,12,4},{12,4,18,5},{18,5,22,8},
        {5,12,8,10},{8,10,12,9},{12,9,16,10},{16,10,19,12},
        {8,16,12,14},{12,14,16,16},{12,20,12,20}
    };
    static const int8_t ble[][4] = {
        {12,2,12,22},{12,2,19,7},{19,7,5,17},
        {5,7,19,17},{19,17,12,22}
    };
    const int8_t (*segments)[4] = bluetooth ? ble : wifi;
    unsigned count = bluetooth ? sizeof ble / sizeof ble[0] : sizeof wifi / sizeof wifi[0];
    uint32_t ink = wireless_color(connected, night);
    for(unsigned i = 0; i < count; ++i)
        line(ctx, x+segments[i][0], y+segments[i][1], x+segments[i][2], y+segments[i][3], ink);
    if(!connected) line(ctx, x+2, y+2, x+22, y+22, ink);
}
#endif
