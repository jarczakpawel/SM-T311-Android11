#!/usr/bin/env python3
"""G767: host-only reproducibility gate for SM-T311 active Wi-Fi / USB prebuilts.

Two indispensable inputs to the G761 Android 11 build were physically present
but untracked in device-common Git. Pin exact bytes; never modify tablet.
"""
from pathlib import Path
import hashlib
import struct
import sys

COMMON = Path(__file__).resolve().parents[1]
ROOT = COMMON.parents[2]
FILES = {
    "known-good-v20260730/smt311-usb-v115": (
        464072,
        "4b9eca615e6e77cec8ca2bbee4e59f35ad4f41e56c8f573377c27ba81a46812f",
        ROOT / "out/target/product/lt013g/system/bin/smt311-usb-v115",
    ),
    "known-good-v20260730/wpa_supplicant": (
        1769484,
        "1b370440b9a4951f9b2f299fea2034cc8dd329021afa10b6f7b75e480e3f6a83",
        ROOT / "out/target/product/lt013g/system/vendor/bin/hw/wpa_supplicant",
    ),
}


def check(path: Path, length: int, expected: str) -> None:
    if not path.is_file() or path.is_symlink() or path.stat().st_size != length:
        raise ValueError(f"G767_FAIL_GEOMETRY: {path}")
    digest = hashlib.sha256()
    with path.open("rb") as src:
        head = src.read(20)
        digest.update(head)
        for chunk in iter(lambda: src.read(1048576), b""):
            digest.update(chunk)
    if head[:4] != b"\x7fELF" or head[4] != 1 or struct.unpack_from("<H", head, 18)[0] != 40:
        raise ValueError(f"G767_FAIL_ARM32_ELF: {path}")
    if digest.hexdigest() != expected:
        raise ValueError(f"G767_FAIL_SHA: {path}")


def verify(check_build: bool = False) -> None:
    mk = (COMMON / "common.mk").read_text()
    bp = (COMMON / "known-good-v20260730/Android.bp").read_text()
    usb_bp = (COMMON / "g812-usb-bootstrap/Android.bp").read_text()
    usb_source = COMMON / "g812-usb-bootstrap/g807_usb_bootstrap.c"
    if "PRODUCT_PACKAGES += smt311-usb-v115-source-g807" not in mk:
        raise ValueError("G767_FAIL_G812_USB_PRODUCT_REFERENCE")
    if ('name: "smt311-usb-v115-source-g807"' not in usb_bp
            or 'srcs: ["g807_usb_bootstrap.c"]' not in usb_bp
            or 'stem: "smt311-usb-v115"' not in usb_bp):
        raise ValueError("G767_FAIL_G812_USB_SOONG_REFERENCE")
    if hashlib.sha256(usb_source.read_bytes()).hexdigest() != "3c01f83f7020a74dd32ec648bab906a66b3aaebb964c04fe2a3c28862256497f":
        raise ValueError("G767_FAIL_G812_USB_SOURCE_SHA")
    if "smt311_wpa_supplicant_known_good" not in mk:
        raise ValueError("G767_FAIL_WIFI_PRODUCT_REFERENCE")
    if 'srcs: ["wpa_supplicant"]' not in bp or 'stem: "wpa_supplicant"' not in bp:
        raise ValueError("G767_FAIL_WIFI_SOONG_REFERENCE")
    for rel, (length, expected, installed) in FILES.items():
        check(COMMON / rel, length, expected)
        if check_build:
            if rel.endswith("/smt311-usb-v115"):
                check(installed, 174644, "0fe2bfe260a177d118aa80d1b324c387097a9cd485a52a48db2b61efd3e46e26")
            else:
                check(installed, length, expected)
        print(f"G767_PASS: {rel} sha256={expected}")
    print("G767_RELEASE_BUILD_INPUTS_PASS, NO_DEVICE_WRITES")


if __name__ == "__main__":
    try:
        verify(check_build="--check-built" in sys.argv[1:])
    except (OSError, ValueError) as e:
        sys.exit(str(e))
