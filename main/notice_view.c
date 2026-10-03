#include "notice_view.h"
#include "fonts/hud_font.h"
#include "moto/notification_icons.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static uint32_t next(const char **text) {
    const unsigned char *p = (const unsigned char *)*text;
    uint32_t c = *p++; unsigned n = 0;
    if (c >= 0xc2 && c <= 0xdf) { c &= 31; n = 1; }
    else if (c >= 0xe0 && c <= 0xef) { c &= 15; n = 2; }
    else if (c >= 0xf0 && c <= 0xf4) { c &= 7; n = 3; }
    else if (c >= 128) c = 0x25a1;
    while (n--) {
        if ((*p & 0xc0) != 0x80) { c = 0x25a1; break; }
        c = (c << 6) | (*p++ & 63);
    }
    *text = (const char *)p; return c;
}
static const hud_glyph *glyph(uint32_t c) {
    unsigned lo = 0, hi = hud_font_count;
    while (lo < hi) { unsigned mid = lo + (hi-lo)/2;
        if (hud_font[mid].codepoint < c) lo = mid+1; else hi = mid; }
    if (lo < hud_font_count && hud_font[lo].codepoint == c) return &hud_font[lo];
    return c == 0x25a1 ? NULL : glyph(0x25a1);
}
static unsigned text_width(const char *s) {
    unsigned width = 0;
    while (*s && *s != '\n' && *s != '\r') {
        const hud_glyph *g = glyph(next(&s));
        width += g ? g->advance : 20;
    }
    return width;
}
/* Fit complete UTF-8 codepoints, reserving ellipsis and the app-name suffix. */
static unsigned fit_text(char *out, size_t capacity, const char *s, unsigned width) {
    unsigned dots = text_width("...");
    bool shortened = text_width(s) > width;
    unsigned budget = shortened ? (width > dots ? width - dots : 0) : width;
    const char *begin = s;
    unsigned drawn = 0;
    while (*s && *s != '\n' && *s != '\r') {
        const char *before = s;
        const hud_glyph *g = glyph(next(&s));
        unsigned advance = g ? g->advance : 20;
        if (drawn + advance > budget || (size_t)(s - begin) + 4 >= capacity) {
            s = before; shortened = true; break;
        }
        drawn += advance;
    }
    size_t length = (size_t)(s - begin);
    memcpy(out, begin, length);
    out[length] = 0;
    if (shortened && width >= dots && length + 4 <= capacity) {
        memcpy(out + length, "...", 4);
        drawn += dots;
    }
    return drawn;
}
static void line(uint16_t *p, unsigned w, unsigned x, unsigned y, unsigned end,
                 unsigned scale, const char *s, uint16_t color) {
    while (*s && *s != '\n' && *s != '\r') {
        const hud_glyph *g = glyph(next(&s));
        if (!g) continue;
        if (x + g->advance * scale > end) break;
        for (unsigned row = 0; row < 24; ++row)
            for (unsigned col = 0; col < 20 && x + (col + 1) * scale <= end; ++col)
                if (g->bits[row*3+col/8] & (0x80 >> (col%8)))
                    for (unsigned dy=0; dy<scale; ++dy)
                        for (unsigned dx=0; dx<scale; ++dx)
                            p[(y+row*scale+dy)*w+x+col*scale+dx] = color;
        x += g->advance*scale;
    }
}
static void icon_draw(uint16_t *pixels, unsigned stride, unsigned x, unsigned y,
                      unsigned size, const moto_notification_icon *icon) {
    for (unsigned row=0; row<size; ++row)
        for (unsigned col=0; col<size; ++col) {
            const uint8_t *p = icon->pixels + (row*icon->height/size)*icon->stride +
                               (col*icon->width/size)*4;
            unsigned a = p[3];
            unsigned r = (p[0]*a + 255*(255-a) + 127)/255;
            unsigned g = (p[1]*a + 255*(255-a) + 127)/255;
            unsigned b = (p[2]*a + 255*(255-a) + 127)/255;
            pixels[(y+row)*stride+x+col] = (uint16_t)(((r>>3)<<11)|((g>>2)<<5)|(b>>3));
        }
}
void terminal_notice_draw(uint16_t *p, unsigned w, unsigned h, const terminal_notice *n) {
    if (!p || !n || w < 160 || h < 80) return;
    unsigned scale = w >= 640 && h >= 160 ? 2 : 1;
    unsigned card_w = w - 24*scale;
    if (card_w > 800) card_w = 800;
    unsigned left = (w-card_w)/2, top = 8*scale, card_h = 60*scale, radius = 10*scale;
    /* Same white rounded banner as S3, centred for the wider P4 display. */
    for (unsigned y=0; y<card_h; ++y)
        for (unsigned x=0; x<card_w; ++x) {
            int dx = x < radius ? (int)(radius-x) : x >= card_w-radius ? (int)(x-(card_w-radius-1)) : 0;
            int dy = y < radius ? (int)(radius-y) : y >= card_h-radius ? (int)(y-(card_h-radius-1)) : 0;
            if (dx*dx + dy*dy <= (int)(radius*radius)) p[(top+y)*w+left+x] = 0xffff;
        }
    moto_notification_presentation app = moto_notification_resolve(n->app);
    icon_draw(p,w,left+12*scale,top+8*scale,32*scale,app.icon);
    unsigned text_x = left+52*scale, text_end = left+card_w-12*scale;
    unsigned width = (text_end-text_x)/scale;
    const char *title = n->title[0] ? n->title : "新通知";
    char header[600], fitted_title[300], fitted_app[260], body[1404];
    if (app.has_app_icon) {
        fit_text(header,sizeof header,title,width);
    } else {
        const char *name = app.app_name ? app.app_name : n->app[0] ? n->app : "未知应用";
        unsigned suffix = text_width(" ()");
        unsigned app_width = fit_text(fitted_app,sizeof fitted_app,name,width/2);
        unsigned title_width = width > app_width+suffix ? width-app_width-suffix : 0;
        fit_text(fitted_title,sizeof fitted_title,title,title_width);
        snprintf(header,sizeof header,"%s (%s)",fitted_title,fitted_app);
    }
    fit_text(body,sizeof body,n->text[0] ? n->text : "收到一条新通知",width);
    line(p,w,text_x,top+5*scale,text_end,scale,header,0x1083);
    line(p,w,text_x,top+30*scale,text_end,scale,body,0x1083);
}
