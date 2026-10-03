#!/usr/bin/env python3
"""Fetch the exact public LVGL 9.3.0 commit; never overwrite an existing checkout."""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / 'vendor/lvgl'
COMMIT = 'c033a98afddd65aaafeebea625382a94020fe4a7'
if DEST.exists():
    current = subprocess.check_output(['git', '-C', str(DEST), 'rev-parse', 'HEAD'], text=True).strip()
    if current != COMMIT:
        raise SystemExit('Existing LVGL has a different revision; use a separate checkout')
else:
    DEST.parent.mkdir(exist_ok=True)
    # A failed fetch leaves no partial destination; the same command can retry.
    with tempfile.TemporaryDirectory(dir=DEST.parent, prefix='.lvgl-fetch-') as folder:
        checkout = Path(folder) / 'lvgl'
        subprocess.run(['git', 'init', str(checkout)], check=True)
        subprocess.run(['git', '-C', str(checkout), 'remote', 'add', 'origin', 'https://github.com/lvgl/lvgl.git'], check=True)
        subprocess.run(['git', '-C', str(checkout), 'fetch', '--depth=1', 'origin', COMMIT], check=True)
        subprocess.run(['git', '-C', str(checkout), 'checkout', '--detach', 'FETCH_HEAD'], check=True)
        checkout.rename(DEST)
print('LVGL 9.3.0 pinned commit ready:', COMMIT)
