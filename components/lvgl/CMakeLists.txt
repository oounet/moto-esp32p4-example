set(LVGL_DIR "${CMAKE_CURRENT_LIST_DIR}/../../vendor/lvgl" CACHE PATH "LVGL 9.3.0 source")
if(NOT EXISTS "${LVGL_DIR}/lvgl.h")
    message(FATAL_ERROR "Run python3 tools/fetch_lvgl.py first, or set -DLVGL_DIR to LVGL 9.3.0")
endif()
file(GLOB_RECURSE LVGL_SOURCES "${LVGL_DIR}/src/*.c" "${LVGL_DIR}/src/*.cpp")
idf_component_register(SRCS ${LVGL_SOURCES}
    INCLUDE_DIRS "." "${LVGL_DIR}" "${LVGL_DIR}/src" "${LVGL_DIR}/.."
    REQUIRES esp_timer freertos)
target_compile_definitions(${COMPONENT_LIB} PUBLIC LV_CONF_INCLUDE_SIMPLE LV_KCONFIG_IGNORE)
target_include_directories(${COMPONENT_LIB} PRIVATE
    "${IDF_PATH}/components/freertos/FreeRTOS-Kernel/include/freertos")
