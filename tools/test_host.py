#!/usr/bin/env python3
"""Self-contained board arithmetic and renderer checks; no device or private sources."""
from pathlib import Path
import os
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
CC = os.environ.get('CC', 'cc')
INCLUDE = ROOT / 'vendor/moto-sdk/include'
if not INCLUDE.is_dir(): raise SystemExit('Import the binary SDK headers first')
for script in ['test_h264_memory.py', 'test_video_color.py']:
    subprocess.run(['python3', str(ROOT / 'tools' / script)], check=True)
with tempfile.TemporaryDirectory(prefix='p4-host-') as folder:
    for name, sources in {
        'ad_receipts': ['tests/test_ad_receipts.c', 'examples/managed-terminal/snippets/ad_receipts.c'],
        'instrument_frame': ['tests/test_instrument_frame.c'],
        'cockpit_map': ['tests/test_cockpit_map.c'],
        'hud_view': ['tests/test_hud_view.c', 'main/hud_view.c', 'main/fonts/hud_font.c'],
    }.items():
        output = Path(folder) / name
        subprocess.run([CC, '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
            '-I', str(ROOT/'tests/stubs'), '-I', str(ROOT/'examples/managed-terminal/snippets'), '-I', str(ROOT/'main'), '-I', str(INCLUDE), *[str(ROOT/p) for p in sources], '-lm', '-o', str(output)], check=True)
        subprocess.run([str(output)], check=True)
        print(name, 'passed (ASan/UBSan)')
subprocess.run(['python3', '-m', 'unittest', 'discover', '-s', str(ROOT/'tests'), '-p', 'test_*.py'], check=True)
print('Host tests complete; these do not prove board radio/display behavior')
