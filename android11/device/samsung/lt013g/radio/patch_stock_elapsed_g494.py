#!/usr/bin/env python3
"""G494: skip absent kernel alarm device; initialize 1e9 multiplier before BOOTTIME.

G490 incorrectly jumped from the failed /dev/alarm open to 0x9d6b0 without
setting r4 to 1000000000 at 0x9d6a0. Clock times were potentially corrupted.
Retain the OEM clock_gettime code, Thumb ABI, stack and existing G350 patch.
"""
from hashlib import sha256
from pathlib import Path
import sys

from patch_stock_elapsed_g490 import patch as g490_patch

G490_SHA = '9f8b20251d0fa378022f8cac0366395f1c5ae52c21315bc34347e9318613f65b'
PATCHES = (
    # After r5/r6 GOT load: branch directly to the original r4 multiplier load.
    (0x9D638, bytes.fromhex('3768'), bytes.fromhex('32e0')),
    # Avoid ioctl on nonexistent fd; continue at CLOCK_BOOTTIME timespec setup.
    # Both instructions occupy the original 32-bit BLX site (B.n + NOP).
    (0x9D6A2, bytes.fromhex('fdf704eb'), bytes.fromhex('05e000bf')),
)

def patch(raw):
    source = g490_patch(raw)
    if sha256(source).hexdigest() != G490_SHA:
        raise ValueError('Unexpected G490 exact binary hash')
    out = bytearray(source)
    for offset, old, new in PATCHES:
        if out[offset:offset + len(old)] != old:
            raise ValueError(f'Unexpected ARM/Thumb instruction at {offset:#x}')
        out[offset:offset + len(old)] = new
    return bytes(out)

if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit('usage: patch_stock_elapsed_g494.py ORIGINAL_OEM_SO OUTPUT_PATH')
    dest = Path(sys.argv[2])
    if dest.exists():
        raise SystemExit('Refusing to overwrite output')
    data = patch(Path(sys.argv[1]).read_bytes())
    dest.write_bytes(data)
    print(sha256(data).hexdigest())
