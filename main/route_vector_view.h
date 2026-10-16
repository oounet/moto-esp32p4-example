#pragma once
#include "moto/vector_map.h"
/* Pure RGB565 renderer, no heap/UI/JPEG/OS calls. Caller owns validated immutable
 * map and framebuffer. Returns whether all viewport blocks are present, including
 * explicit empty blocks. Source map completeness is not a routing guarantee.
 * The owner supplies the last accepted position and computes fresh from its
 * receive time/TTL. This stateless compositor never advances or expires it. */
bool route_vector_draw(uint16_t *pixels, unsigned width, unsigned height,
    const moto_vector_map *, const moto_position *, bool fresh, bool night);

/* Before a road window is ready, draw only the authenticated route and position. */
bool route_vector_draw_navigation(uint16_t *,unsigned,unsigned,const moto_route_point *,unsigned,
    const moto_position *,bool fresh,bool night);
