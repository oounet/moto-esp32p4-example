cmake_minimum_required(VERSION 3.20)
set(COMPONENTS main)
set(IDF_TARGET esp32p4 CACHE STRING "Reference board target")
set(MOTO_SDK_DIR "${CMAKE_CURRENT_LIST_DIR}/vendor/moto-sdk" CACHE PATH "Extracted binary SDK")
if(NOT EXISTS "${MOTO_SDK_DIR}/components/moto_prebuilt/CMakeLists.txt")
    message(FATAL_ERROR "Import the binary SDK first: python3 tools/import_sdk.py /path/to/package.zip")
endif()
if(NOT EXISTS "${MOTO_SDK_DIR}/include/moto/route_map.h")
    message(FATAL_ERROR "Import the matching revision 11 / FreeRTOS ABI 6 binary SDK; update headers and archives together.")
endif()
set(SDKCONFIG_DEFAULTS "${MOTO_SDK_DIR}/components/moto_prebuilt/sdkconfig.defaults;${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
set(EXTRA_COMPONENT_DIRS "${MOTO_SDK_DIR}/components/moto_prebuilt")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(moto_p4_example VERSION 0.1.0)

# Scope the SIMD memory-capability fix to tinyH264's precompiled allocator calls.
# Keep the managed download intact and rebuild the derived archive when it changes.
idf_component_get_property(h264_dir espressif__esp_h264 COMPONENT_DIR)
idf_component_get_property(h264_lib espressif__esp_h264 COMPONENT_LIB)
if(CONFIG_ESP32P4_SELECTS_REV_LESS_V3)
    set(h264_profile esp32p4_less_v3)
else()
    set(h264_profile esp32p4)
endif()
set(h264_original "${h264_dir}/sw/libs/${h264_profile}/libtinyh264.a")
set(h264_simd "${CMAKE_BINARY_DIR}/libtinyh264_simd.a")
add_custom_command(OUTPUT "${h264_simd}"
    COMMAND ${CMAKE_OBJCOPY}
        --redefine-sym heap_caps_malloc_prefer=moto_h264_malloc_prefer
        --redefine-sym heap_caps_calloc_prefer=moto_h264_calloc_prefer
        --redefine-sym malloc=moto_h264_malloc
        "${h264_original}" "${h264_simd}"
    DEPENDS "${h264_original}" VERBATIM)
add_custom_target(h264_simd_archive DEPENDS "${h264_simd}")
add_dependencies(${h264_lib} h264_simd_archive)
get_target_property(h264_links ${h264_lib} INTERFACE_LINK_LIBRARIES)
list(FIND h264_links tinyh264 h264_index)
if(h264_index EQUAL -1)
    message(FATAL_ERROR "tinyH264 link contract changed; review scoped SIMD allocator adaptation")
endif()
list(REMOVE_AT h264_links ${h264_index})
list(INSERT h264_links ${h264_index} "${h264_simd}")
set_property(TARGET ${h264_lib} PROPERTY INTERFACE_LINK_LIBRARIES "${h264_links}")
# The prebuilt archive is linked after component archives. Pull in its allocator
# object from main before that scan (all three allocator entry points share it).
target_link_options(${h264_lib} INTERFACE "LINKER:-u,moto_h264_malloc_prefer")
