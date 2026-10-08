#pragma once
#include "moto/route_map.h"
/* Pure RGB565 compositor; owns no buffers. All geometry is world pixels at z18. */
typedef struct { double x,y; } route_map_xy;
unsigned route_map_project_route(const moto_map_package *, route_map_xy *, unsigned capacity);
bool route_map_draw(uint16_t *,unsigned width,unsigned height,const moto_map_package *,
    const route_map_xy *,unsigned count,const moto_position *,bool fresh);

/* Optional platform decoder. Called only for viewport rows. */
typedef bool (*route_map_row_reader)(void *,const moto_map_package *,unsigned,uint32_t,uint32_t,unsigned,uint16_t[256]);
bool route_map_draw_tiles(uint16_t *,unsigned,unsigned,const moto_map_package *,const route_map_xy *,unsigned,
    const moto_position *,bool,route_map_row_reader,void *);
