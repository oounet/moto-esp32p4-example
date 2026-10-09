#include "route_map_store.h"
#include "route_map_view.h"
#include "route_map_compression.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/jpeg_decode.h"
#include <stdlib.h>
#include <string.h>
static SemaphoreHandle_t mutex;
static uint8_t *staging,*front;
static uint32_t staging_size;
static moto_map_package package;
static route_map_xy *detail,coarse[MOTO_ROUTE_MAX_POINTS];
static unsigned coarse_count,detail_count;
static uint64_t map_nav,map_route,nav,route,generation;
static moto_position position;
static bool positioned;
static int64_t position_until;
/* Decoded tiles stay in a bounded LRU, independent of video buffers. Mutex owned. */
#define TILE_CACHE 20
static struct {uint8_t *pixels;size_t capacity;uint64_t key,used;bool present,decoded;} cache[TILE_CACHE];
static uint64_t cache_clock;
static jpeg_decoder_handle_t map_decoder;
static void clear_cache(void){
    for(unsigned i=0;i<TILE_CACHE;i++){free(cache[i].pixels);memset(&cache[i],0,sizeof cache[i]);}
    if(map_decoder){jpeg_del_decoder_engine(map_decoder);map_decoder=NULL;}
    cache_clock=0;
}
static bool read_row(void *u,const moto_map_package *m,unsigned z,uint32_t x,uint32_t y,unsigned row,uint16_t out[256]){
    (void)u;unsigned format;moto_slice bytes;
    if(!moto_map_tile_get(m,z,x,y,&format,&bytes))return false;
    if(format!=2)return moto_map_tile_row(m,z,x,y,row,out);
    uint64_t key=((uint64_t)z<<40)|((uint64_t)x<<20)|y;unsigned slot=0;bool hit=false;
    for(unsigned i=0;i<TILE_CACHE;i++){
        if(cache[i].present && cache[i].key==key){slot=i;hit=true;break;}
        if(cache[i].used<cache[slot].used)slot=i;
    }
    if(!hit){
        cache[slot].key=key;cache[slot].present=true;cache[slot].decoded=false;
        if(!map_decoder){jpeg_decode_engine_cfg_t cfg={.timeout_ms=50};jpeg_new_decoder_engine(&cfg,&map_decoder);}
        if(!cache[slot].pixels){jpeg_decode_memory_alloc_cfg_t memory={.buffer_direction=JPEG_DEC_ALLOC_OUTPUT_BUFFER};
            cache[slot].pixels=jpeg_alloc_decoder_mem(131072,&memory,&cache[slot].capacity);}
        if(map_decoder && cache[slot].pixels){uint32_t size=0;jpeg_decode_cfg_t cfg={.output_format=JPEG_DECODE_OUT_FORMAT_RGB565,.rgb_order=JPEG_DEC_RGB_ELEMENT_ORDER_RGB};
            cache[slot].decoded=jpeg_decoder_process(map_decoder,&cfg,bytes.data,bytes.size,cache[slot].pixels,cache[slot].capacity,&size)==ESP_OK && size==131072;}
    }
    cache[slot].used=++cache_clock;if(!cache[slot].decoded)return false;
    const uint8_t *p=cache[slot].pixels+row*512;for(unsigned i=0;i<256;i++)out[i]=((uint16_t)p[i*2]<<8)|p[i*2+1];return true;
}
static int begin(void *u,uint32_t size){
    (void)u;free(staging);staging=NULL;staging_size=0;
    /* Leave room for the renderer/video decoder and TLS during overlap. */
    if(size>4u*1024u*1024u || heap_caps_get_free_size(MALLOC_CAP_SPIRAM)<size+4u*1024u*1024u)return MOTO_FULL;
    staging=heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!staging)return MOTO_FULL;
    staging_size=size;ESP_LOGI("MotoMap","staging bytes=%lu",(unsigned long)size);return MOTO_OK;
}
static int write_map(void *u,uint32_t offset,moto_slice bytes){
    (void)u;if(!staging||offset>staging_size||bytes.size>staging_size-offset)return MOTO_INVALID;
    memcpy(staging+offset,bytes.data,bytes.size);return MOTO_OK;
}
static int commit(void *u,uint64_t navigation,uint64_t revision,uint32_t size){
    (void)u;moto_map_package next;
    if(!staging||size!=staging_size)return MOTO_INVALID;
    uint8_t *expanded=NULL,*candidate=staging;size_t raw_size=size;
    if(size>=4&&!memcmp(staging,"MZP1",4)){
        raw_size=route_map_expanded_size(staging,size,4u*1024u*1024u);if(!raw_size)return MOTO_INVALID;
        if(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)<raw_size+4u*1024u*1024u)return MOTO_FULL;
        expanded=heap_caps_malloc(raw_size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!expanded)return MOTO_FULL;
        int64_t started=esp_timer_get_time();
        if(!route_map_inflate(staging,size,expanded,raw_size)){free(expanded);return MOTO_INVALID;}
        ESP_LOGI("MotoMap","inflated bytes=%u raw=%u ms=%u",(unsigned)size,(unsigned)raw_size,(unsigned)((esp_timer_get_time()-started)/1000));candidate=expanded;
    }
    if(!moto_map_package_read((moto_slice){candidate,raw_size},&next)){free(expanded);return MOTO_INVALID;}
    route_map_xy *points=heap_caps_malloc(next.route_points*sizeof *points,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!points){free(expanded);return MOTO_FULL;}
    unsigned count=route_map_project_route(&next,points,next.route_points);
    xSemaphoreTake(mutex,portMAX_DELAY);
    uint8_t *old=front;route_map_xy *old_points=detail;
    clear_cache();
    uint8_t *compressed=expanded?staging:NULL;
    front=candidate;staging=NULL;staging_size=0;package=next;detail=points;detail_count=count;
    map_nav=navigation;map_route=revision;++generation;
    xSemaphoreGive(mutex);free(compressed);free(old);free(old_points);ESP_LOGI("MotoMap","committed bytes=%lu",(unsigned long)size);return MOTO_OK;
}
static void reset(void *u,bool all){
    (void)u;free(staging);staging=NULL;staging_size=0;
    if(!all)return;
    xSemaphoreTake(mutex,portMAX_DELAY);uint8_t *old=front;route_map_xy *points=detail;
    clear_cache();
    front=NULL;detail=NULL;memset(&package,0,sizeof package);map_nav=map_route=0;detail_count=0;++generation;
    xSemaphoreGive(mutex);free(old);free(points);
}
moto_map_port terminal_map_port(void){
    if(!mutex)mutex=xSemaphoreCreateMutex();
    return mutex?(moto_map_port){NULL,4u*1024u*1024u,begin,write_map,commit,reset,true}:(moto_map_port){0};
}
void terminal_map_event(const moto_event *e){
    if(!mutex)return;
    xSemaphoreTake(mutex,portMAX_DELAY);
    switch(e->kind){
    case MOTO_EVENT_ROUTE:
        nav=e->route->id;route=e->route->revision;coarse_count=e->route->count;positioned=false;
        for(unsigned i=0;i<coarse_count;i++)moto_map_project(e->route->points[i],18,&coarse[i].x,&coarse[i].y);
        ++generation;break;
    case MOTO_EVENT_POSITION:
        if(!positioned)ESP_LOGI("MotoMap","first position received");
        position=*e->position;positioned=true;position_until=esp_timer_get_time()/1000+position.ttl_ms;++generation;break;
    case MOTO_EVENT_POSITION_EXPIRED:position_until=0;++generation;break;
    case MOTO_EVENT_NAV_END:case MOTO_EVENT_CLOSED:case MOTO_EVENT_UNPAIR:
        nav=route=0;positioned=false;coarse_count=0;++generation;break;
    default:break;
    }xSemaphoreGive(mutex);
}
bool terminal_map_changed(void){
    if(!mutex)return false;
    static uint64_t previous;static bool was_fresh;
    xSemaphoreTake(mutex,portMAX_DELAY);bool fresh=positioned&&esp_timer_get_time()/1000<position_until;
    bool changed=previous!=generation||was_fresh!=fresh;previous=generation;was_fresh=fresh;
    xSemaphoreGive(mutex);return changed;
}
bool terminal_map_draw(uint16_t *pixels,unsigned w,unsigned h,bool *fresh,bool *complete){
    if(!mutex)return false;
    xSemaphoreTake(mutex,portMAX_DELAY);
    if(!positioned||!nav||position.id!=nav||position.route_revision!=route){xSemaphoreGive(mutex);return false;}
    bool detailed=map_nav==nav&&map_route==route&&detail;
    *fresh=esp_timer_get_time()/1000<position_until;
    *complete=route_map_draw_tiles(pixels,w,h,map_nav==nav?&package:NULL,
        detailed?detail:coarse,detailed?detail_count:coarse_count,&position,*fresh,read_row,NULL);
    xSemaphoreGive(mutex);return true;
}
