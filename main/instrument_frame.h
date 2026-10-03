#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

enum { ART_W = 1080, ART_H = 608, LCD_W = 1024, LCD_H = 600,
       COCKPIT_TOP = 16, COCKPIT_H = 558 };

/* The cockpit filter retains fine lines in its textured artwork. */
static inline uint16_t instrument_rgb565_mix(uint16_t a, uint16_t b, unsigned weight)
{
    // Six fractional bits fit between packed R/B lanes without cross-channel
    // carries. Round each lane; this also preserves constant colours exactly.
    unsigned rb = ((a & 0xf81f) * (64-weight) + (b & 0xf81f) * weight + 0x10020) >> 6;
    unsigned g = ((a & 0x07e0) * (64-weight) + (b & 0x07e0) * weight + 0x400) >> 6;
    return (uint16_t)((rb & 0xf81f) | (g & 0x07e0));
}
static inline void instrument_frame_filter_row(uint16_t *out,const uint16_t *row,
                                                const uint32_t coordinates[LCD_W])
{
    for(unsigned x=0;x<LCD_W;x++) {
        unsigned sx=coordinates[x]>>6,weight=coordinates[x]&63;
        unsigned nx=sx+1<ART_W?sx+1:sx;
        out[x]=instrument_rgb565_mix(row[sx],row[nx],weight);
    }
}
static inline void instrument_frame_compose_region(uint16_t *target, const uint16_t *source,
                                                    unsigned source_top, unsigned source_height)
{
    // Pixel-center bilinear sampling retains hairlines. Cache horizontally
    // filtered rows on the task's internal-RAM stack; adjacent output rows
    // share a source row, so do not read/filter that PSRAM row twice.
    uint32_t coordinates[LCD_W];
    uint16_t rows[2][LCD_W],*top=rows[0],*bottom=rows[1];
    unsigned bottom_y=ART_H;
    for(unsigned x=0;x<LCD_W;x++)
        coordinates[x]=((2*x+1)*ART_W*64+LCD_W)/(2*LCD_W)-32;
    for(unsigned y=0;y<LCD_H;y++) {
        // Map the selected artwork region to all 600 LCD rows.
        unsigned fy=((2*y+1)*source_height*64+LCD_H)/(2*LCD_H)+source_top*64-32;
        unsigned sy=fy>>6,wy=fy&63,ny=sy+1<ART_H?sy+1:sy;
        if(sy==bottom_y) {uint16_t *swap=top;top=bottom;bottom=swap;}
        else instrument_frame_filter_row(top,source+sy*ART_W,coordinates);
        instrument_frame_filter_row(bottom,source+ny*ART_W,coordinates);
        bottom_y=ny;
        uint16_t *out=target+y*LCD_W;
        for(unsigned x=0;x<LCD_W;x++)out[x]=instrument_rgb565_mix(top[x],bottom[x],wy);
    }
}

/* Restore the classic's original lightweight sampling. Full-screen geometry
 * only changes the destination height from 576 to 600; it must not add the
 * cockpit's expensive per-pixel filter to every animated classic frame.
 * Always overwrite both complete buffers, including after video playback. */
static inline void instrument_frame_compose(uint16_t *target, const uint16_t *source,
                                            const uint16_t source_x[LCD_W])
{
    for (unsigned y = 0; y < LCD_H; ++y) {
        const uint16_t *row = source + (y * ART_H / LCD_H) * ART_W;
        uint16_t *out = target + y * LCD_W;
        for (unsigned x = 0; x < LCD_W; ++x) out[x] = row[source_x[x]];
    }
}

/* Preserve the cockpit crop that excludes its decorative vertical padding. */
static inline void instrument_frame_compose_full(uint16_t *target, const uint16_t *source,
                                                 const uint16_t source_x[LCD_W])
{
    (void)source_x;
    instrument_frame_compose_region(target, source, COCKPIT_TOP, COCKPIT_H);
}

/* Update only pixels whose sampling footprint intersects a changed source
 * rectangle. Keep the full-frame sampler's exact RGB565 rounding and crop. */
static inline void instrument_frame_update(uint16_t *target, const uint16_t *source,
                                          const uint16_t source_x[LCD_W], bool cockpit,
                                          int x1, int y1, int x2, int y2)
{
    for (unsigned y = 0; y < LCD_H; ++y) {
        unsigned fy = cockpit ? ((2*y+1)*COCKPIT_H*64+LCD_H)/(2*LCD_H)+COCKPIT_TOP*64-32 : 0;
        unsigned sy = cockpit ? fy>>6 : y*ART_H/LCD_H;
        unsigned ny = cockpit && sy+1<ART_H ? sy+1 : sy;
        if ((int)ny < y1 || (int)sy > y2) continue;
        for (unsigned x = 0; x < LCD_W; ++x) {
            unsigned fx = cockpit ? ((2*x+1)*ART_W*64+LCD_W)/(2*LCD_W)-32 : 0;
            unsigned sx = cockpit ? fx>>6 : source_x[x];
            unsigned nx = cockpit && sx+1<ART_W ? sx+1 : sx;
            if ((int)nx < x1 || (int)sx > x2) continue;
            if (cockpit) {
                uint16_t top = instrument_rgb565_mix(source[sy*ART_W+sx],source[sy*ART_W+nx],fx&63);
                uint16_t bottom = instrument_rgb565_mix(source[ny*ART_W+sx],source[ny*ART_W+nx],fx&63);
                target[y*LCD_W+x] = instrument_rgb565_mix(top,bottom,fy&63);
            } else target[y*LCD_W+x] = source[sy*ART_W+sx];
        }
    }
}
