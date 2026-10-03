#pragma once
#include <stdint.h>

/* PPA YUV420 is packed UYY on the first row, VYY on the next row.
 * tinyH264 I420 planes use the coded height, including any bottom padding. */
static inline void video_color_pack(const uint8_t *i420, unsigned width,
                                   unsigned coded_height, unsigned height, uint8_t *packed)
{
    const uint8_t *u = i420 + width * coded_height;
    const uint8_t *v = u + width * coded_height / 4;
    for (unsigned y = 0; y < height; ++y) {
        const uint8_t *chroma = (y & 1 ? v : u) + (y / 2) * (width / 2);
        const uint8_t *luma = i420 + y * width;
        for (unsigned x = 0; x < width; x += 2) {
            *packed++ = *chroma++;
            *packed++ = *luma++;
            *packed++ = *luma++;
        }
    }
}
