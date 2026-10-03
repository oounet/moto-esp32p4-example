#pragma once
#include <stdint.h>

/* Single video worker owns conversion. Output must be cache-aligned PSRAM.
 * Initialization checks hardware output before enabling it; CPU is the fallback. */
void video_color_init(unsigned width, unsigned height, uint16_t *output);
void video_color_convert(const uint8_t *i420, unsigned coded_height, uint16_t *output);
