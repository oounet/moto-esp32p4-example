#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "cockpit_map_curve.h"

/* Geometry belongs to the 1080x608 cockpit artwork. The instrument compositor
 * crops rows 16..573 to 600 LCD rows; use that same transform for both sources. */
enum { COCKPIT_MAP_X = 680, COCKPIT_MAP_Y = 124, COCKPIT_MAP_SIDE = 448,
       COCKPIT_MAP_CENTER_X = 904, COCKPIT_MAP_CENTER_Y = 348 };
static inline unsigned cockpit_map_left(unsigned y) {
    double sy = (y + 0.5) * 558.0 / 600.0 + 16.0;
    if (sy < 132 || sy >= 548) return 1024;
    double x;
    if (sy <= 425) x = cockpit_map_curve[(unsigned)sy-132];
    else if (sy < 505) x = 803 - (sy-425) * 67 / 80;
    else x = 736 + (sy-505) * 35 / 43;
    return (unsigned)ceil(x * 1024 / 1080.0);
}
static inline void cockpit_map_copy(uint16_t *target, const uint16_t *source,
                                    unsigned stride, int origin_x, int origin_y) {
    for (unsigned y=COCKPIT_MAP_Y; y<COCKPIT_MAP_Y+COCKPIT_MAP_SIDE; ++y) {
        unsigned left=cockpit_map_left(y);
        if(left>=1024) continue;
        memcpy(target+y*1024+left, source+(y-origin_y)*stride+left-origin_x,
               (1024-left)*sizeof(uint16_t));
    }
}
