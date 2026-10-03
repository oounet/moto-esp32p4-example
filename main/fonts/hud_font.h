#ifndef HUD_FONT_H
#define HUD_FONT_H
#include <stdint.h>
typedef struct { uint16_t codepoint; uint8_t advance, bits[72]; } hud_glyph;
extern const hud_glyph hud_font[];
extern const unsigned hud_font_count;
#endif
