#include "route_vector_store.h"
#include "route_vector_view.h"
#include "route_map_compression.h"
#ifdef MOTO_VECTOR_STORE_HOST_TEST
#include "vector_store_host.h"
#else
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "mbedtls/sha256.h"
#endif
#include <stdlib.h>
#include <string.h>

typedef struct snapshot {uint8_t *bytes;moto_vector_map map;unsigned refs;} snapshot;
typedef struct job {
    moto_map_vector_identity identity;uint8_t *wire;uint32_t wire_size;
    snapshot *result;int status;bool done,cancelled;
} job;
static SemaphoreHandle_t lock;
static snapshot *front;
static job *pending;
static bool worker_busy;
static uint64_t generation;
static void release(snapshot *s){if(s){free(s->bytes);free(s);}}
static void destroy(job *j){if(j){free(j->wire);release(j->result);free(j);}}
static bool same(const moto_map_vector_identity *a,const moto_map_vector_identity *b){
    return a->navigation==b->navigation&&a->route==b->route&&a->revision==b->revision&&
        a->raw_size==b->raw_size&&!memcmp(a->sha256,b->sha256,32);
}
bool route_vector_store_init(void){if(!lock)lock=xSemaphoreCreateMutex();return lock!=NULL;}
static int validate(job *j){
    uint8_t *raw=j->wire;size_t size=j->wire_size;bool compressed=size>=4&&!memcmp(raw,"RVZ1",4);
    if(compressed){
        size=route_map_expanded_size(raw,size,MOTO_VECTOR_MAX_BYTES);
        if(size!=j->identity.raw_size)return MOTO_INVALID;
        uint8_t *expanded=heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!expanded)return MOTO_FULL;
        if(!route_map_inflate(raw,j->wire_size,expanded,size)){free(expanded);return MOTO_INVALID;}
        free(j->wire);j->wire=raw=expanded;
    }
    if(size!=j->identity.raw_size||size<128||memcmp(raw,"RVP1",4))return MOTO_INVALID;
    uint8_t digest[32];
    if(mbedtls_sha256(raw,size,digest,0)||memcmp(digest,j->identity.sha256,32))return MOTO_INVALID;
    snapshot *s=calloc(1,sizeof *s);if(!s)return MOTO_FULL;
    if(!moto_vector_read((moto_slice){raw,size},&s->map)||s->map.navigation!=j->identity.navigation||
       s->map.route!=j->identity.route||s->map.revision!=j->identity.revision){free(s);return MOTO_INVALID;}
    s->bytes=raw;s->refs=1;j->wire=NULL;j->result=s;return MOTO_OK;
}
static void worker(void *arg){
    job *j=arg;int64_t start=esp_timer_get_time();int result=validate(j);
    xSemaphoreTake(lock,portMAX_DELAY);j->status=result;j->done=true;worker_busy=false;
    bool cancelled=j->cancelled;xSemaphoreGive(lock);
    ESP_LOGI("MotoVector","validated result=%d ms=%u",result,(unsigned)((esp_timer_get_time()-start)/1000));
    if(cancelled)destroy(j);
    vTaskDelete(NULL);
}
int route_vector_store_commit(uint8_t **bytes,const moto_map_vector_identity *identity,uint32_t size){
    if(!lock||!bytes||!identity||size>MOTO_VECTOR_MAX_BYTES)return MOTO_INVALID;
    xSemaphoreTake(lock,portMAX_DELAY);
    if(pending){
        job *j=pending;
        if(!same(&j->identity,identity)||j->wire_size!=size){xSemaphoreGive(lock);return MOTO_STATE;}
        if(!j->done){xSemaphoreGive(lock);return MOTO_AGAIN;}
        pending=NULL;int result=j->status;snapshot *old=NULL;
        if(result==MOTO_OK){old=front;front=j->result;j->result=NULL;++generation;if(old&&--old->refs)old=NULL;}
        xSemaphoreGive(lock);release(old);destroy(j);
        if(result==MOTO_OK)ESP_LOGI("MotoVector","committed revision=%llu wire=%lu raw=%lu",
            (unsigned long long)identity->revision,(unsigned long)size,(unsigned long)identity->raw_size);
        return result;
    }
    if(worker_busy){xSemaphoreGive(lock);return MOTO_AGAIN;}
    if(!*bytes){xSemaphoreGive(lock);return MOTO_INVALID;}
    job *j=calloc(1,sizeof *j);if(!j){xSemaphoreGive(lock);return MOTO_FULL;}
    j->identity=*identity;j->wire=*bytes;j->wire_size=size;*bytes=NULL;pending=j;worker_busy=true;
    xSemaphoreGive(lock);
    if(xTaskCreate(worker,"vector_verify",6144,j,2,NULL)!=pdPASS){
        xSemaphoreTake(lock,portMAX_DELAY);pending=NULL;worker_busy=false;*bytes=j->wire;j->wire=NULL;xSemaphoreGive(lock);
        destroy(j);return MOTO_FULL;
    }
    return MOTO_AGAIN;
}
void route_vector_store_reset(bool all){
    if(!lock)return;
    xSemaphoreTake(lock,portMAX_DELAY);job *j=pending;pending=NULL;snapshot *old=NULL;
    if(j&&!j->done){j->cancelled=true;j=NULL;}
    if(all){old=front;front=NULL;++generation;if(old&&--old->refs)old=NULL;}
    xSemaphoreGive(lock);destroy(j);release(old);
}
bool route_vector_store_available(void){
    if(!lock)return false;
    xSemaphoreTake(lock,portMAX_DELAY);bool ready=front!=NULL;xSemaphoreGive(lock);return ready;
}
bool route_vector_store_changed(void){
    if(!lock)return false;
    static uint64_t previous;xSemaphoreTake(lock,portMAX_DELAY);bool changed=previous!=generation;previous=generation;xSemaphoreGive(lock);return changed;
}
bool route_vector_store_draw(uint16_t *p,unsigned w,unsigned h,const moto_position *position,bool fresh,bool night,bool *complete){
    if(!lock||!position||!complete)return false;
    xSemaphoreTake(lock,portMAX_DELAY);snapshot *s=front;
    if(s&&(s->map.navigation!=position->id||s->map.route!=position->route_revision))s=NULL;
    if(s)++s->refs;
    xSemaphoreGive(lock);if(!s)return false;
    *complete=route_vector_draw(p,w,h,&s->map,position,fresh,night);
    xSemaphoreTake(lock,portMAX_DELAY);bool last=--s->refs==0;xSemaphoreGive(lock);if(last)release(s);
    return true;
}
