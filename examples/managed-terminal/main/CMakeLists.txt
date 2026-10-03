idf_component_register(SRCS "main.c" "board_driver.c"
  "../snippets/quick_start.c" "../snippets/license.c" "../snippets/bluetooth.c"
  "../snippets/api.c" "../snippets/vehicle.c" "../snippets/notifications.c"
  "../snippets/optional_features.c" "../snippets/video.c" "../snippets/advertising.c" "../snippets/bulk.c" "../snippets/testing.c" "../snippets/ota.c" "../snippets/faults.c" "../snippets/ad_receipts.c" INCLUDE_DIRS "." REQUIRES moto_prebuilt nvs_flash)
target_compile_definitions(${COMPONENT_LIB} PRIVATE MOTO_DEMO_MJPEG=$<BOOL:${MOTO_DEMO_MJPEG}>)
