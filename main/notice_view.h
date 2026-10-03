#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef struct {
    uint64_t id;
    bool ancs;
    int64_t until;
    char app[257], title[257], text[1401];
} terminal_notice;
/* Pure bounded RGB565 compositor, shared by instrument and video frames. */
void terminal_notice_draw(uint16_t *pixels, unsigned width, unsigned height,
                          const terminal_notice *notice);
