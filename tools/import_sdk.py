#!/usr/bin/env python3
"""Verify and import a supplied proprietary package; never extract private source."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import stat
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]

def import_package(archive, destination):
    if destination.exists():
        raise ValueError("Destination exists; select a new --output for upgrades")
    with zipfile.ZipFile(archive) as z:
        files = [x for x in z.infolist() if not x.is_dir()]
        if not files or len(files) > 256 or sum(x.file_size for x in files) > 32 * 1024 * 1024:
            raise ValueError("Unexpected package size")
        names = set()
        roots = set()
        for item in files:
            p = PurePosixPath(item.filename)
            if p.is_absolute() or '..' in p.parts or len(p.parts) < 2 or '\\' in item.filename:
                raise ValueError("Unsafe package path")
            if stat.S_ISLNK(item.external_attr >> 16) or item.filename in names:
                raise ValueError("Links or duplicate files are not supported")
            names.add(item.filename); roots.add(p.parts[0])
        if len(roots) != 1:
            raise ValueError("Expected one package root")
        prefix = roots.pop() + '/'
        sums = {}
        for line in z.read(prefix + 'SHA256SUMS').decode().splitlines():
            digest, path = line.split('  ', 1)
            if path in sums or len(digest) != 64:
                raise ValueError("Invalid checksum manifest")
            sums[path] = digest
        actual = {n[len(prefix):] for n in names} - {'SHA256SUMS'}
        if set(sums) != actual:
            raise ValueError("Checksum file set differs from archive")
        for rel, digest in sums.items():
            if hashlib.sha256(z.read(prefix + rel)).hexdigest() != digest:
                raise ValueError("Checksum mismatch: " + rel)
        info = json.loads(z.read(prefix + 'BUILD_INFO.json'))
        if (info.get('distribution'), info.get('target'), info.get('idf_version'), info.get('freertos_abi')) != ('binary-only', 'esp32p4', '5.5.4', 6):
            raise ValueError("Requires binary ESP32-P4 / IDF 5.5.4 / ABI 6 package")
        selected = {p for p in actual if p.startswith(('include/', 'lib/', 'components/moto_prebuilt/', 'third_party_notices/')) or p in {'BUILD_INFO.json', 'VALIDATION.json', 'THIRD_PARTY_NOTICES.txt'}}
        required = {'include/moto/sdk.h', 'include/moto_freertos/terminal.h', 'components/moto_prebuilt/CMakeLists.txt', 'lib/esp32p4-idf5.5.4/libmoto_sdk.a', 'lib/esp32p4-idf5.5.4/libmoto_freertos.a'}
        if not required <= selected or any(PurePosixPath(p).suffix in {'.c', '.cpp', '.py', '.motolic', '.pem', '.key'} for p in selected):
            raise ValueError("Unexpected SDK payload")
        destination.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=destination.parent, prefix='.sdk-import-') as folder:
            stage = Path(folder) / 'sdk'; stage.mkdir()
            for rel in sorted(selected):
                target = stage / rel; target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(z.read(prefix + rel))
            (stage / 'SHA256SUMS').write_text(''.join(sums[p] + '  ' + p + '\n' for p in sorted(selected)))
            stage.rename(destination)
    print(f"Verified binary SDK imported: {len(selected)} files")

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('--output', type=Path, default=ROOT / 'vendor/moto-sdk')
    args = parser.parse_args()
    try:
        import_package(args.archive, args.output.resolve())
    except (ValueError, KeyError, zipfile.BadZipFile) as e:
        parser.exit(1, str(e) + '\n')
