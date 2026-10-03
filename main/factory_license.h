#pragma once
#include "nvs.h"
#include "moto/license.h"
/* Initialize once before tasks. All import/load calls serialize on the storage mutex. */
void factory_license_init(nvs_handle_t storage,const char *hardware_id);
const char *factory_license_hardware_id(void);
int factory_license_load(moto_license *out);
/* Any trusted production transport can pass the same 512 signed bytes here.
 * Identical repeat is OK; a different installed license is ESP_ERR_INVALID_STATE.
 * A successful new import requires an application-controlled reboot. */
esp_err_t factory_license_install(const uint8_t *blob,size_t size);

/* Remove only owner pairing state; immutable License remains installed. */
esp_err_t factory_license_clear_owner(void);
