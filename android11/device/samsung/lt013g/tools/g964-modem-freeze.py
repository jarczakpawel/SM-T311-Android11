#!/usr/bin/env python3
"""SM-T311 G964 - fail closed: never replace the currently accepted RIL.

This is a BUILD/RELEASE guard only. No modem, SIM, device or PIN actions.
Remove this hold only after an explicit separate request to modify radio.
"""
import hashlib
import sys
from pathlib import Path

ANDROID = Path(__file__).resolve().parents[4]
PRODUCT = ANDROID / "out/target/product/lt013g"
ACTIVE_RIL_SHA256 = "4e6bb8a9465005a01f5bebd92a7e0ad247c0c4b609809c2600547bc97999bcb7"
MODULE = PRODUCT / "system/vendor/lib/libril.so"

def main():
    if sys.argv[1:] != ["--built"]:
        sys.exit("G964 MODEM FREEZE: usage --built")
    if not MODULE.is_file():
        sys.exit("G964 BLOCKED: no built RIL, cannot guarantee modem preservation")
    h = hashlib.sha256()
    with MODULE.open("rb") as src:
        for block in iter(lambda: src.read(1048576), b""):
            h.update(block)
    actual = h.hexdigest()
    if actual != ACTIVE_RIL_SHA256:
        sys.exit("G964 BLOCKED: ROM would replace the accepted RIL library; "
                 "modem/SIM/PIN/PUK are frozen; do not flash SYSTEM. "
                 f"host RIL sha256={actual}")
    print("G964_MODEM_FROZEN_PASS: exact accepted libril SHA, no modem changes")

if __name__ == "__main__":
    main()
