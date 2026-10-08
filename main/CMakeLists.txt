idf_component_register(SRCS
    "main.c" "terminal.c" "terminal_config.c" "terminal_board.c" "terminal_video.c"
    "h264_memory.c" "video_color.c" "factory_usb.c" "factory_license.c" "ancs_adapter.c"
    "notice_view.c" "hud_view.c" "fonts/hud_font.c" "ui/electric_ui.c"
    "route_map_compression.c" "route_map_store.c" "route_map_transport.c" "route_map_view.c"
    INCLUDE_DIRS "."
    EMBED_FILES "assets/jpeg_selftest.jpg"
    REQUIRES lvgl jd9165 esp_lcd esp_driver_gpio esp_driver_ledc esp_timer esp_psram
    moto_prebuilt bt nvs_flash esp_app_format json driver esp_wifi esp_netif esp_event
    lwip esp_h264 esp_hosted esp_driver_ppa esp_driver_jpeg)
if(MOTO_LICENSE_TRUST_HEADER)
    get_filename_component(moto_trust "${MOTO_LICENSE_TRUST_HEADER}" ABSOLUTE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE MOTO_LICENSE_TRUST_HEADER="${moto_trust}")
endif()
