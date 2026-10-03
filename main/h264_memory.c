/* tinyH264 1.4.0's P4 binary requests INTERNAL/8BIT for SIMD work buffers.
 * RTCRAM and TCM satisfy those flags but cannot service 128-bit PIE accesses.
 * Only this decoder archive is redirected here; other allocators are unchanged. */
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include "esp_heap_caps.h"

static void *allocate(size_t size, size_t count, va_list args)
{
    for (size_t i = 0; i < count; ++i) {
        uint32_t caps = va_arg(args, uint32_t);
        void *p = heap_caps_aligned_alloc(16, size, caps | MALLOC_CAP_SIMD);
        if (p) return p;
    }
    return NULL;
}
void *moto_h264_malloc_prefer(size_t size, size_t count, ...)
{
    va_list args; va_start(args, count);
    void *p = allocate(size, count, args);
    va_end(args); return p;
}
void *moto_h264_calloc_prefer(size_t n, size_t size, size_t count, ...)
{
    if (size && n > SIZE_MAX / size) return NULL;
    va_list args; va_start(args, count);
    void *p = allocate(n * size, count, args);
    va_end(args);
    if (p) memset(p, 0, n * size);
    return p;
}
void *moto_h264_malloc(size_t size)
{
    return heap_caps_aligned_alloc(16, size, MALLOC_CAP_8BIT | MALLOC_CAP_SIMD);
}
