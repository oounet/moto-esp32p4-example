/* Transport-independent ESP NVS adapter; core parser also works on RTOS/Linux. */
#include "factory_license.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#ifdef MOTO_LICENSE_TRUST_HEADER
#include MOTO_LICENSE_TRUST_HEADER
#else
static const uint8_t moto_license_trust_key[65]={0};
#endif
static nvs_handle_t store;
static char hardware[65];
static SemaphoreHandle_t mutex;
void factory_license_init(nvs_handle_t storage,const char *hardware_id){
 store=storage;snprintf(hardware,sizeof hardware,"%s",hardware_id);
 mutex=xSemaphoreCreateMutex();configASSERT(mutex);
}
const char *factory_license_hardware_id(void){return hardware;}
int factory_license_load(moto_license *out){
 uint8_t blob[MOTO_LICENSE_SIZE];size_t size=sizeof blob;
 xSemaphoreTake(mutex,portMAX_DELAY);esp_err_t rc=nvs_get_blob(store,"license_v1",blob,&size);xSemaphoreGive(mutex);
 int result=rc==ESP_OK?moto_license_open(blob,size,hardware,moto_license_verify_p256,(void *)moto_license_trust_key,out):-1;
 if(result) moto_license_clear(out);
 memset(blob,0,sizeof blob);return result;
}
esp_err_t factory_license_install(const uint8_t *blob,size_t size){
 moto_license license;
 if(moto_license_open(blob,size,hardware,moto_license_verify_p256,(void *)moto_license_trust_key,&license))return ESP_ERR_INVALID_ARG;
 moto_license_clear(&license);
 uint8_t existing[MOTO_LICENSE_SIZE];size_t old_size=sizeof existing;
 xSemaphoreTake(mutex,portMAX_DELAY);
 esp_err_t rc=nvs_get_blob(store,"license_v1",existing,&old_size);
 if(rc==ESP_ERR_NVS_NOT_FOUND){rc=nvs_set_blob(store,"license_v1",blob,size);if(rc==ESP_OK)rc=nvs_commit(store);}
 else rc=rc==ESP_OK&&old_size==size&&!memcmp(existing,blob,size)?ESP_OK:ESP_ERR_INVALID_STATE;
 xSemaphoreGive(mutex);memset(existing,0,sizeof existing);return rc;
}

esp_err_t factory_license_clear_owner(void){
 xSemaphoreTake(mutex,portMAX_DELAY);
 esp_err_t rc=nvs_erase_key(store,"license_auth");
 if(rc==ESP_ERR_NVS_NOT_FOUND)rc=ESP_OK;
 if(rc==ESP_OK)rc=nvs_commit(store);
 xSemaphoreGive(mutex);return rc;
}
