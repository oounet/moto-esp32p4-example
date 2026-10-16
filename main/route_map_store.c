#include "route_map_store.h"
#include "route_vector_view.h"
#include "route_vector_store.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>
static SemaphoreHandle_t mutex;
static uint8_t *staging;
static uint32_t staging_size;
static moto_route_point coarse[MOTO_ROUTE_MAX_POINTS];
static unsigned coarse_count;
static uint64_t nav,route,generation;
static moto_position position;
static bool positioned;
static int64_t position_until;
static int begin(void *u,uint32_t size){
    (void)u;free(staging);staging=NULL;staging_size=0;
    if(!size||size>MOTO_VECTOR_MAX_BYTES || heap_caps_get_free_size(MALLOC_CAP_SPIRAM)<size+4u*1024u*1024u)return MOTO_FULL;
    staging=heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!staging)return MOTO_FULL;
    staging_size=size;return MOTO_OK;
}
static int write_map(void *u,uint32_t offset,moto_slice bytes){
    (void)u;if(!staging||offset>staging_size||bytes.size>staging_size-offset)return MOTO_INVALID;
    memcpy(staging+offset,bytes.data,bytes.size);return MOTO_OK;
}
/* The SDK's required generic callback remains present; raster payloads are not supported. */
static int reject_raster(void *u,uint64_t navigation,uint64_t revision,uint32_t size){
    (void)u;(void)navigation;(void)revision;(void)size;return MOTO_INVALID;
}
static void reset(void *u,bool all){
    (void)u;route_vector_store_reset(all);free(staging);staging=NULL;staging_size=0;
}
static int commit_vector(void *u,const moto_map_vector_identity *identity,uint32_t size){
    (void)u;if(size!=staging_size)return MOTO_INVALID;
    xSemaphoreTake(mutex,portMAX_DELAY);bool current=nav==identity->navigation&&route==identity->route;xSemaphoreGive(mutex);
    if(!current){route_vector_store_reset(false);return MOTO_EXPIRED;}
    int rc=route_vector_store_commit(&staging,identity,size);
    if(rc==MOTO_OK || (rc<0 && rc!=MOTO_AGAIN && !staging))staging_size=0;
    return rc;
}
moto_map_port terminal_map_port(void){
    if(!mutex)mutex=xSemaphoreCreateMutex();
    if(!mutex||!route_vector_store_init())return (moto_map_port){0};
    return (moto_map_port){NULL,MOTO_VECTOR_MAX_BYTES,begin,write_map,reject_raster,reset,true,commit_vector};
}
void terminal_map_event(const moto_event *e){
    if(!mutex)return;
    xSemaphoreTake(mutex,portMAX_DELAY);
    switch(e->kind){
    case MOTO_EVENT_ROUTE:
        nav=e->route->id;route=e->route->revision;coarse_count=e->route->count;positioned=false;
        memcpy(coarse,e->route->points,coarse_count*sizeof *coarse);++generation;break;
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
    xSemaphoreGive(mutex);return route_vector_store_changed()||changed;
}
bool terminal_map_draw(uint16_t *pixels,unsigned w,unsigned h,bool night,bool *fresh,bool *complete){
    if(!mutex||!fresh||!complete)return false;
    moto_route_point points[MOTO_ROUTE_MAX_POINTS];
    xSemaphoreTake(mutex,portMAX_DELAY);
    if(!positioned||!nav||position.id!=nav||position.route_revision!=route){xSemaphoreGive(mutex);return false;}
    *fresh=esp_timer_get_time()/1000<position_until;
    moto_position copy=position;unsigned count=coarse_count;memcpy(points,coarse,count*sizeof *points);xSemaphoreGive(mutex);
    if(route_vector_store_draw(pixels,w,h,&copy,*fresh,night,complete))return true;
    *complete=false;
    return route_vector_draw_navigation(pixels,w,h,points,count,&copy,*fresh,night);
}
bool terminal_map_vector_available(void){return route_vector_store_available();}
