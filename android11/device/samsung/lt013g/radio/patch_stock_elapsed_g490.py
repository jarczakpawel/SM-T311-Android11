#!/usr/bin/env python3
from hashlib import sha256
from pathlib import Path
import sys

from patch_stock_mutex_g350 import PATCHED, transform

PATCHES = (
    (0x9D682, bytes.fromhex('49e0'), bytes.fromhex('15e0')),
    (0x9D6B4, bytes.fromhex('0120'), bytes.fromhex('0720')),
)

def patch(binary):
    original = transform(binary)
    if sha256(original).hexdigest() != PATCHED:
        raise ValueError('Unexpected G350 base')
    result = bytearray(original)
    for offset, before, after in PATCHES:
        if result[offset:offset + len(before)] != before:
            raise ValueError(f'Unexpected OEM instruction at {offset:#x}')
        result[offset:offset + len(before)] = after
    return bytes(result)

if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit('usage: patch_stock_elapsed_g490.py ORIGINAL_OEM_LIBSEC_RIL OUTPUT_CANDIDATE')
    destination = Path(sys.argv[2])
    if destination.exists():
        raise SystemExit('Output exists; refusing to overwrite')
    data = patch(Path(sys.argv[1]).read_bytes())
    destination.write_bytes(data)
    print(sha256(data).hexdigest())
