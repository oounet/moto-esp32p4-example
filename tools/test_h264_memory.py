#!/usr/bin/env python3
"""Host regression for decoder-only SIMD allocation, fallback and calloc bounds."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="moto-h264-memory-") as folder:
    work = Path(folder)
    (work / "esp_heap_caps.h").write_text("""
#include <stddef.h>
#include <stdint.h>
#define MALLOC_CAP_8BIT (1u << 2)
#define MALLOC_CAP_SIMD (1u << 20)
void *heap_caps_aligned_alloc(size_t, size_t, uint32_t);
""")
    (work / "test.c").write_text("""
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
void *moto_h264_malloc(size_t);
void *moto_h264_malloc_prefer(size_t, size_t, ...);
void *moto_h264_calloc_prefer(size_t, size_t, size_t, ...);
static unsigned calls, fail_count;
static uint32_t seen[8];
void *heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t caps) {
    assert(alignment == 16 && (caps & MALLOC_CAP_SIMD));
    seen[calls++] = caps;
    if (fail_count) { --fail_count; return NULL; }
    void *p = NULL;
    if (posix_memalign(&p, alignment, size ? size : 16)) return NULL;
    memset(p, 0xa5, size); return p;
}
int main(void) {
    unsigned char *p = moto_h264_malloc(31);
    assert(p && !((uintptr_t)p & 15)); free(p);
    calls = 0; fail_count = 1;
    p = moto_h264_malloc_prefer(64, 2, 0x804u, 0x404u);
    assert(p && calls == 2 && seen[0] == (0x804u | MALLOC_CAP_SIMD));
    assert(seen[1] == (0x404u | MALLOC_CAP_SIMD)); free(p);
    calls = 0;
    p = moto_h264_calloc_prefer(3, 17, 2, 0x804u, 0x404u);
    assert(p && calls == 1);
    for (unsigned i = 0; i < 51; ++i) assert(p[i] == 0);
    free(p); calls = 0;
    assert(!moto_h264_calloc_prefer(SIZE_MAX, 2, 1, 0x804u));
    assert(calls == 0);
    fail_count = 2;
    assert(!moto_h264_malloc_prefer(64, 2, 0x804u, 0x404u));
    assert(calls == 2);
    assert(!moto_h264_malloc_prefer(64, 0));
    return 0;
}
""")
    binary = work / "test"
    subprocess.run([
        "cc", "-std=c11", "-D_POSIX_C_SOURCE=200112L", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-I", str(work),
        str(root / "main/h264_memory.c"), str(work / "test.c"), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
print("H264 SIMD allocator: alignment, capabilities, fallback, zeroing, overflow and OOM passed (ASan/UBSan).")
