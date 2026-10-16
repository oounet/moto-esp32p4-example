#pragma once
#include <pthread.h>
#include <stdlib.h>
#include <stdint.h>
#include <CommonCrypto/CommonDigest.h>
typedef pthread_mutex_t *SemaphoreHandle_t;
#define portMAX_DELAY 0
#define pdPASS 1
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define ESP_LOGI(...) ((void)0)
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void){static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;return &lock;}
static inline void xSemaphoreTake(SemaphoreHandle_t m,int wait){(void)wait;pthread_mutex_lock(m);}
static inline void xSemaphoreGive(SemaphoreHandle_t m){pthread_mutex_unlock(m);}
static inline void *heap_caps_malloc(size_t size,int flags){(void)flags;return malloc(size);}
static inline int64_t esp_timer_get_time(void){return 0;}
static inline int mbedtls_sha256(const unsigned char *p,size_t n,unsigned char *out,int is224){(void)is224;return CC_SHA256(p,(CC_LONG)n,out)?0:-1;}
int xTaskCreate(void (*run)(void *),const char *,unsigned,void *,unsigned,void *);
void vTaskDelete(void *);
