#include "route_vector_store.h"
#include "route_vector_view.h"
#include "vector_store_host.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <zlib.h>
static void (*task)(void *);static void *argument;static bool task_failure,reset_on_draw;
int xTaskCreate(void (*run)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *out){
    (void)name;(void)stack;(void)priority;(void)out;assert(!task);
    if(task_failure)return 0;
    task=run;argument=arg;return 1;
}
void vTaskDelete(void *p){(void)p;}
static void run_worker(void){assert(task);void (*fn)(void *)=task;void *arg=argument;task=NULL;argument=NULL;fn(arg);}
bool route_vector_draw(uint16_t *p,unsigned w,unsigned h,const moto_vector_map *m,const moto_position *pos,bool fresh,bool night){
    (void)p;(void)w;(void)h;(void)pos;(void)fresh;(void)night;
    if(reset_on_draw)route_vector_store_reset(true);
    /* ASan verifies the render reference survives a concurrent front removal. */
    assert(!memcmp(m->bytes.data,"RVP1",4));return true;
}
static void put(uint8_t *p,uint32_t n){p[0]=n>>24;p[1]=n>>16;p[2]=n>>8;p[3]=n;}
static uint8_t *fixture(moto_map_vector_identity *identity,unsigned revision,uint32_t *size,bool compressed){
    uint8_t raw[168]={0};memcpy(raw,"RVP1\0\1\0\200",8);put(raw+12,7);put(raw+20,9);put(raw+28,revision);
    put(raw+32,MOTO_VECTOR_WORLD);put(raw+36,1);put(raw+48,2);put(raw+52,24);put(raw+60,1);
    *identity=(moto_map_vector_identity){.navigation=7,.route=9,.revision=revision,.raw_size=sizeof raw};
    mbedtls_sha256(raw,sizeof raw,identity->sha256,0);
    uint8_t *result=malloc(300);*size=sizeof raw;memcpy(result,raw,sizeof raw);
    if(compressed){uLongf length=288;memcpy(result,"RVZ1",4);put(result+4,sizeof raw);put(result+8,(uint32_t)crc32(0,raw,sizeof raw));
        assert(compress2(result+12,&length,raw,sizeof raw,3)==Z_OK);*size=(uint32_t)length+12;}
    return result;
}
int main(void){
    assert(route_vector_store_init());moto_map_vector_identity id;uint32_t size;uint8_t *bytes=fixture(&id,1,&size,true);
    assert(route_vector_store_commit(&bytes,&id,size)==MOTO_AGAIN&&!bytes&&!route_vector_store_available());
    assert(route_vector_store_commit(&bytes,&id,size)==MOTO_AGAIN);run_worker();
    assert(route_vector_store_commit(&bytes,&id,size)==MOTO_OK&&route_vector_store_available());
    assert(route_vector_store_changed()&&!route_vector_store_changed());
    bytes=fixture(&id,2,&size,false);id.sha256[0]^=1;
    assert(route_vector_store_commit(&bytes,&id,size)==MOTO_AGAIN);run_worker();
    assert(route_vector_store_commit(&bytes,&id,size)==MOTO_INVALID&&route_vector_store_available());
    bytes=fixture(&id,3,&size,false);assert(route_vector_store_commit(&bytes,&id,size)==MOTO_AGAIN);route_vector_store_reset(false);
    bytes=fixture(&id,4,&size,false);uint8_t *saved=bytes;
    assert(route_vector_store_commit(&bytes,&id,size)==MOTO_AGAIN&&bytes==saved);run_worker();
    assert(route_vector_store_commit(&bytes,&id,size)==MOTO_AGAIN&&!bytes);run_worker();
    assert(route_vector_store_commit(&bytes,&id,size)==MOTO_OK);
    moto_position position={.id=7,.route_revision=9};bool complete=false;uint16_t pixel=0;
    reset_on_draw=true;assert(route_vector_store_draw(&pixel,320,200,&position,true,false,&complete)&&complete);
    assert(!route_vector_store_available());reset_on_draw=false;
    bytes=fixture(&id,5,&size,false);assert(route_vector_store_commit(&bytes,&id,size)==MOTO_AGAIN);run_worker();route_vector_store_reset(true);
    assert(!route_vector_store_available());
    bytes=fixture(&id,6,&size,false);saved=bytes;task_failure=true;
    assert(route_vector_store_commit(&bytes,&id,size)==MOTO_FULL&&bytes==saved);free(bytes);
    route_vector_store_reset(true);
    puts("vector store: async validation, RVZ1/SHA, cancellation, immutable refs, atomic replace and task failure passed");
}
