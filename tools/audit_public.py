#!/usr/bin/env python3
"""Audit the exact Git index before publication, without printing sensitive content."""
from pathlib import Path
import re
import subprocess
ROOT = Path(__file__).resolve().parents[1]
paths = subprocess.check_output(['git', 'ls-files', '-z'], cwd=ROOT).decode().strip('\0').split('\0')
if not paths or paths == ['']:
    raise SystemExit('Stage intended public files before running this index audit')
problems = []
for rel in paths:
    p = Path(rel)
    if p.parts[0] in {'vendor', 'build', 'managed_components', 'artifacts', 'local'} or p.suffix in {'.a', '.bin', '.elf', '.map', '.zip', '.motolic', '.pem', '.key', '.credentials'} or p.name == 'sdkconfig':
        problems.append(rel + ': forbidden artifact'); continue
    raw = subprocess.check_output(['git', 'show', ':' + rel], cwd=ROOT)
    # Do not mistake the audit's own patterns for secrets.
    if rel == 'tools/audit_public.py': continue
    if re.search(rb'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----|/Users/[^/]+/|gh[pousr]_[A-Za-z0-9]{30,}', raw):
        problems.append(rel + ': private material or local path')
    if re.search(rb'\.\./moto-sdk/(?:src|ports)|\.\./lvgl-simulator|F101S3/', raw):
        problems.append(rel + ': private workspace dependency')
if problems: raise SystemExit('\n'.join(problems))
print(f'Public index audit passed: {len(paths)} files; no SDK archives, private sources or credential artifacts')
