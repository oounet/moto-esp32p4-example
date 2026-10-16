#include "route_vector_view.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static uint8_t data[512];
static void put(size_t at,uint32_t value){
    data[at]=(uint8_t)(value>>24);data[at+1]=(uint8_t)(value>>16);
    data[at+2]=(uint8_t)(value>>8);data[at+3]=(uint8_t)value;
}
static size_t variable(size_t at,int delta){
    unsigned value=delta<0?(unsigned)(-delta*2-1):(unsigned)delta*2;
    while(value>=128){data[at++]=(uint8_t)((value&127)|128);value>>=7;}
    data[at++]=(uint8_t)value;return at;
}
static size_t feature(size_t at,int layer,unsigned flags,unsigned x,unsigned y,int dx,int dy){
    size_t start=at;data[at]=1;data[at+1]=(uint8_t)flags;data[at+2]=(uint8_t)layer;data[at+5]=2;
    at+=8;data[at++]=(uint8_t)(x>>8);data[at++]=(uint8_t)x;data[at++]=(uint8_t)(y>>8);data[at++]=(uint8_t)y;
    at=variable(at,dx);at=variable(at,dy);data[start+7]=(uint8_t)(at-start-8);return at;
}
static moto_vector_map fixture(bool missing){
    memset(data,0,sizeof data);memcpy(data,"RVP1\0\1\0\200",8);
    put(12,7);put(20,9);put(28,1);put(32,MOTO_VECTOR_WORLD);put(36,4);put(40,2);put(44,4);
    put(48,2);put(52,96);put(60,1);put(136,1);put(140,1); /* route outside viewport */
    unsigned block=0;
    for(unsigned x=16383;x<=16384;x++)for(unsigned y=16383;y<=16384;y++){
        size_t at=144+(block++)*24;put(at,x);put(at+4,y);
    }
    if(missing)put(144,16382); /* legal index, but the northwest viewport block is unknown */
    size_t end=feature(240,0,0,400,200,800,0);
    end=feature(end,1,1,800,0,0,400);
    put(56,(uint32_t)(end-240));put(228,(uint32_t)(end-240));put(232,2);put(236,4);
    moto_vector_map map;assert(moto_vector_read((moto_slice){data,end},&map));return map;
}
static bool has(const uint16_t *pixels,size_t count,uint16_t color){
    for(size_t i=0;i<count;i++)if(pixels[i]==color)return true;
    return false;
}
int main(void){
    const unsigned w=1024,h=600;const size_t count=(size_t)w*h;
    uint16_t *buffer=malloc((count+2)*sizeof *buffer);assert(buffer);
    uint16_t *pixels=buffer+1;buffer[0]=buffer[count+1]=0x1234;
    moto_vector_map map=fixture(false);moto_position position={0};position.id=7;position.route_revision=9;
    assert(route_vector_draw(pixels,w,h,&map,&position,true,false));
    assert(pixels[0]==0xef5d&&has(pixels,count,0x03bf));
    assert(pixels[350*w+650]==0x8c71); /* ground road */
    assert(pixels[350*w+709]==0xef5d); /* bridge casing hides ground crossing */
    assert(pixels[350*w+712]==0x8c71); /* bridge deck */
    uint32_t fresh_shape=0,stale_shape=0;
    for(size_t i=0;i<count;i++)if(pixels[i]==0x03bf)fresh_shape+=(uint32_t)i;
    assert(route_vector_draw(pixels,w,h,&map,&position,false,false));
    assert(!has(pixels,count,0x03bf)&&has(pixels,count,0x8410));
    for(size_t i=0;i<count;i++)if(pixels[i]==0x8410)stale_shape+=(uint32_t)i;
    assert(fresh_shape==stale_shape); /* same actual position, only freshness color changes */
    assert(route_vector_draw(pixels,w,h,&map,&position,true,true));assert(pixels[0]==0x1082);
    uint16_t before=pixels[0];position.route_revision++;
    assert(!route_vector_draw(pixels,w,h,&map,&position,true,false)&&pixels[0]==before);
    position.route_revision=9;position.id++;
    assert(!route_vector_draw(pixels,w,h,&map,&position,true,false));position.id=7;
    assert(!route_vector_draw(pixels,319,h,&map,&position,true,false));
    position.point.longitude_e6=180000001;
    assert(!route_vector_draw(pixels,w,h,&map,&position,true,false));position.point.longitude_e6=0;
    map=fixture(true);assert(!route_vector_draw(pixels,w,h,&map,&position,true,false));
    assert(has(pixels,count,0x03bf)); /* missing coverage must not hide the valid position */
    map=fixture(false);put(128,MOTO_VECTOR_WORLD/4);put(132,0);put(136,3*MOTO_VECTOR_WORLD/4);put(140,MOTO_VECTOR_WORLD);
    assert(moto_vector_read(map.bytes,&map)); /* enormous line is clipped before rasterizing */
    assert(route_vector_draw(pixels,w,h,&map,&position,true,false));assert(has(pixels,count,0x059f));
    /* Both sides of the date line are adjacent; a crossing route stays local. */
    map=fixture(false);put(144,0);put(168,0);put(192,32767);put(216,32767);
    put(128,MOTO_VECTOR_WORLD-800);put(132,MOTO_VECTOR_WORLD/2+400);
    put(136,800);put(140,MOTO_VECTOR_WORLD/2+400);
    assert(moto_vector_read(map.bytes,&map));position.point.longitude_e6=180000000;
    assert(route_vector_draw(pixels,w,h,&map,&position,true,false));
    assert(pixels[400*w+450]==0x059f&&pixels[400*w+570]==0x059f);
    position.point.longitude_e6=-180000000;
    assert(route_vector_draw(pixels,w,h,&map,&position,true,false));
    assert(pixels[400*w+450]==0x059f&&pixels[400*w+570]==0x059f);
    /* No downloaded roads: keep route and real position, without tile rendering. */
    moto_route_point coarse[]={{1000,179999000},{1000,-179999000}};
    assert(route_vector_draw_navigation(pixels,w,h,coarse,2,&position,true,true));
    assert(pixels[0]==0x1082&&has(pixels,count,0x059f)&&has(pixels,count,0x03bf));
    assert(!route_vector_draw_navigation(pixels,319,h,coarse,2,&position,true,true));
    assert(buffer[0]==0x1234&&buffer[count+1]==0x1234);free(buffer);
}
