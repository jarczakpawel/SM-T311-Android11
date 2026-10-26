#!/usr/bin/env python3
"""Private-input Samsung SM-T311 libsec-ril.so compatibility transform.

No OEM binaries are checked into the device source tree. The original
T311XXUBNI1 binary remains untouched. The output is built only from an
operator-provided, SHA-checked stock input during a local ROM build.

Samsung List::StartIteration -> List::GetHeadPosition locks the same
mutex twice. List::InitList selects PTHREAD_MUTEX_RECURSIVE, but passes
an uninitialized stack pthread_mutexattr_t to settype(). On modern
Bionic, garbage bits can cause mutex_init() failure; caller ignores
its return and then deadlocks on a NORMAL mutex.

Both List constructors already initialize list->count=0. Replace the
redundant count zero assignment with stack attr zero initialization.
"""
from __future__ import annotations
import hashlib
import pathlib
import sys

ORIGINAL = "432658fd34c966a8ed16b9548ca8d015383d26c6c732826a43363131d31e3a94"
PATCHED = "0a6739936a0981f6541012f55948f8e5b70f2c17f64c25bd5dabbb8e3f1452e9"
OFFSET = 0x9D1E6
BEFORE = bytes.fromhex("2561")  # Thumb: str r5,[r4,#16]
AFTER = bytes.fromhex("0195")   # Thumb: str r5,[sp,#4]

def transform(binary: bytes) -> bytes:
    digest = hashlib.sha256(binary).hexdigest()
    if digest == PATCHED:
        assert binary[OFFSET:OFFSET+2] == AFTER
        return binary
    if digest != ORIGINAL or binary[OFFSET:OFFSET+2] != BEFORE:
        raise ValueError("Unknown stock libsec-ril.so: refusing compatibility modification")
    if binary[OFFSET-2:OFFSET+2] != bytes.fromhex("a6602561"):
        raise ValueError("List::InitList preconditions not present")
    out=bytearray(binary)
    out[OFFSET:OFFSET+2]=AFTER
    out=bytes(out)
    if hashlib.sha256(out).hexdigest()!=PATCHED:
        raise ValueError("Unexpected patched digest")
    return out

if __name__=="__main__":
    if len(sys.argv)!=3:
        raise SystemExit("usage: patch_stock_mutex_g350.py INPUT_OEM_LIBSEC_RIL OUTPUT_PRIVATE")
    source=pathlib.Path(sys.argv[1])
    dest=pathlib.Path(sys.argv[2])
    dest.write_bytes(transform(source.read_bytes()))
    print("G350 private RIL, SHA256:",hashlib.sha256(dest.read_bytes()).hexdigest())
