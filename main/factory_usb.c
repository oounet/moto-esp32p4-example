/* Optional USB transport for the same signed License used by all platforms.
 * No unsigned device identities, no License private material or credential rotation.
 * Explicit wifi_start/wifi_configure returns manual-join AP credentials locally. */
#include "factory_usb.h"
#include "factory_license.h"
#include "terminal_board.h"
#include "terminal_video.h"
#include "moto/license.h"
#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "driver/usb_serial_jtag.h"
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
static void reply(void){
 moto_license license;bool valid=factory_license_load(&license)==0;
 cJSON *out=cJSON_CreateObject();if(!out){moto_license_clear(&license);return;}
 cJSON_AddStringToObject(out,"hardwareId",factory_license_hardware_id());cJSON_AddStringToObject(out,"terminalId",valid?license.terminal_id:factory_license_hardware_id());
 cJSON_AddBoolToObject(out,"configured",valid);cJSON_AddStringToObject(out,"hardwareRevision","JC1060-ESP32-P4-C6");
 cJSON_AddStringToObject(out,"firmwareVersion",esp_app_get_description()->version);
 cJSON_AddBoolToObject(out,"bluetoothReady",moto_terminal_ble_ready());
 cJSON_AddBoolToObject(out,"decoderReady",moto_terminal_video_ready());
 cJSON_AddBoolToObject(out,"videoActive",terminal_video_active());
 cJSON_AddBoolToObject(out,"videoVisible",terminal_video_visible());
 cJSON_AddBoolToObject(out,"videoPreview",terminal_video_preview());
 cJSON_AddStringToObject(out,"instrumentStyle",terminal_board_instrument());
 if(valid){cJSON_AddStringToObject(out,"serialNumber",license.serial);cJSON_AddStringToObject(out,"discoveryId",license.discovery_id);}
 char *encoded=cJSON_PrintUnformatted(out);if(encoded){printf("MOTO_FACTORY_INFO %s\n",encoded);cJSON_free(encoded);}cJSON_Delete(out);moto_license_clear(&license);
}
static void receive(cJSON *json){
 const cJSON *command=cJSON_GetObjectItemCaseSensitive(json,"command");
 if(!cJSON_IsString(command))return;
 if(!strcmp(command->valuestring,"info")){reply();return;}
 if(!strcmp(command->valuestring,"display_hold")){
  terminal_display_hold(30);
  puts("MOTO_DISPLAY_HOLD_QUEUED seconds=30");
  return;
 }
 if(!strcmp(command->valuestring,"display_render_only")){
  terminal_display_test(2);puts("MOTO_DISPLAY_TEST_QUEUED mode=2");return;
 }
 if(!strcmp(command->valuestring,"display_swap_only")){
  terminal_display_test(3);puts("MOTO_DISPLAY_TEST_QUEUED mode=3");return;
 }
 if(!strcmp(command->valuestring,"instrument")){
  const cJSON *style=cJSON_GetObjectItemCaseSensitive(json,"style");
  if(cJSON_IsString(style))puts(terminal_board_select_instrument(style->valuestring)?"MOTO_INSTRUMENT_QUEUED":"MOTO_INSTRUMENT_INVALID");
  else reply();
  return;
 }
 if(!strcmp(command->valuestring,"wifi_start")){
  if(!terminal_video_manual_ap())puts("MOTO_MANUAL_WIFI_NOT_READY");
  return;
 }
 if(!strcmp(command->valuestring,"wifi_configure")){
  const cJSON *ssid=cJSON_GetObjectItemCaseSensitive(json,"ssid");
  const cJSON *password=cJSON_GetObjectItemCaseSensitive(json,"password");
  if(!cJSON_IsString(ssid)||!cJSON_IsString(password)||!terminal_video_configure_ap(ssid->valuestring,password->valuestring))puts("MOTO_MANUAL_WIFI_INVALID");
  return;
 }
 if(strcmp(command->valuestring,"license"))return;
 const cJSON *data=cJSON_GetObjectItemCaseSensitive(json,"data");
 if(!cJSON_IsString(data)||strlen(data->valuestring)!=684)return;
 uint8_t blob[MOTO_LICENSE_SIZE];size_t size=0;
 if(mbedtls_base64_decode(blob,sizeof blob,&size,(const unsigned char *)data->valuestring,strlen(data->valuestring))){puts("MOTO_LICENSE_INVALID");memset(blob,0,sizeof blob);return;}
 esp_err_t rc=factory_license_install(blob,size);memset(blob,0,sizeof blob);
 if(rc!=ESP_OK){puts(rc==ESP_ERR_INVALID_ARG?"MOTO_LICENSE_INVALID":rc==ESP_ERR_INVALID_STATE?"MOTO_LICENSE_ALREADY_CONFIGURED":"MOTO_LICENSE_WRITE_FAILED");return;}
 puts("MOTO_LICENSE_INSTALLED");vTaskDelay(pdMS_TO_TICKS(200));esp_restart();
}

static void task(void *arg){
 (void)arg;char line[1024];size_t used=0;bool overflow=false;reply();
 for(;;){uint8_t ch;if(usb_serial_jtag_read_bytes(&ch,1,pdMS_TO_TICKS(1000))!=1)continue;
  if(ch!='\n'){if(used<sizeof line-1&&!overflow)line[used++]=(char)ch;else overflow=true;continue;}
  line[used]=0;if(!overflow){cJSON *json=cJSON_Parse(line);if(cJSON_IsObject(json))receive(json);cJSON_Delete(json);}
  memset(line,0,sizeof line);used=0;overflow=false;
 }
}
void factory_usb_start(void){
 usb_serial_jtag_driver_config_t config={.rx_buffer_size=2048,.tx_buffer_size=2048};ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&config));
 assert(xTaskCreate(task,"factory_usb",8192,NULL,2,NULL)==pdPASS);
}
