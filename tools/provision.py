#!/usr/bin/env python3
"""Read hardware identity or install a signed device license over USB (no signing)."""
import argparse
import base64
import json
from pathlib import Path
import time

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--license', type=Path, help='512-byte signed .motolic file; omit for read-only info')
    args = parser.parse_args()
    payload = {'command': 'info'}
    if args.license:
        blob = args.license.read_bytes()
        if len(blob) != 512: parser.error('Expected exactly 512 bytes')
        payload = {'command': 'license', 'data': base64.b64encode(blob).decode('ascii')}
    try:
        import serial
    except ImportError:
        parser.error('Run: python3 -m pip install -r tools/requirements.txt')
    # Open with control lines low, avoiding an intentional reset/bootloader entry.
    with serial.Serial(port=None, baudrate=115200, timeout=0.25, write_timeout=2) as device:
        device.dtr = False; device.rts = False; device.port = args.port; device.open()
        device.reset_input_buffer()
        device.write((json.dumps(payload, separators=(',', ':')) + '\n').encode('ascii'))
        device.flush()
        deadline = time.monotonic() + 8
        buffer = bytearray()
        while time.monotonic() < deadline:
            chunk = device.read(256)
            buffer.extend(chunk)
            while b'\n' in buffer:
                line, _, rest = buffer.partition(b'\n'); buffer[:] = rest
                line = line.strip()
                # Do not print arbitrary device logs or the sent license bytes.
                if not args.license and line.startswith(b'MOTO_FACTORY_INFO '):
                    info = json.loads(line[len(b'MOTO_FACTORY_INFO '):])
                    print(json.dumps({k: info.get(k) for k in ('hardwareId', 'configured', 'hardwareRevision', 'firmwareVersion')}, ensure_ascii=False, indent=2))
                    return
                if args.license and line.startswith(b'MOTO_LICENSE_'):
                    status = line.decode('ascii', errors='replace')
                    allowed = {'MOTO_LICENSE_INSTALLED', 'MOTO_LICENSE_INVALID', 'MOTO_LICENSE_ALREADY_CONFIGURED', 'MOTO_LICENSE_WRITE_FAILED'}
                    if status in allowed:
                        print(status)
                        if status != 'MOTO_LICENSE_INSTALLED': raise SystemExit(1)
                        return
            if len(buffer) > 2048: buffer.clear()
        raise SystemExit('No matching reply; stop monitor, check USB port and firmware provisioning setting')

if __name__ == '__main__': main()
