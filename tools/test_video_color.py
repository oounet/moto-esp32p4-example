#!/usr/bin/env python3
"""Check I420 -> PPA packed row layout and coded-height crop under sanitizers."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="moto-video-color-") as folder:
    source = Path(folder) / "test.c"
    source.write_text(r'''
#include <assert.h>
#include <string.h>
#include "video_color_pack.h"
int main(void) {
    /* 4x4 coded image cropped to 4x2: skipped Y rows precede the chroma planes. */
    const uint8_t src[] = {1,2,3,4,5,6,7,8,99,99,99,99,99,99,99,99,
                           10,11,12,13,20,21,22,23};
    const uint8_t expected[] = {10,1,2,11,3,4,20,5,6,21,7,8};
    uint8_t dst[12]; video_color_pack(src,4,4,2,dst);
    assert(!memcmp(dst,expected,sizeof dst));
    /* No-crop multi-pair case checks that U/V advance once per two rows. */
    const uint8_t full[] = {1,2,3,4,5,6,7,8,10,11,20,21};
    const uint8_t wanted[] = {10,1,2,20,3,4,11,5,6,21,7,8};
    video_color_pack(full,2,4,4,dst);
    assert(!memcmp(dst,wanted,sizeof dst));
    return 0;
}
''')
    binary = Path(folder) / "test"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-I", str(root / "main"),
                    str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("PPA packing: U/V row parity, plane offsets and cropped coded height passed (ASan/UBSan).")
