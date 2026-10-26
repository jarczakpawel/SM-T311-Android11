#!/usr/bin/env python3
"""Deterministic Samsung SM-T311 source-release proprietary asset audit.

Read-only. Records paths, hashes, provenance group, Git tracking and presence
of local license evidence. Never copies firmware, SIM/NV, GPS coordinates or
OEM binaries into the output. Legal rights remain subject to actual review.
"""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import tarfile
import zipfile

HERE = Path(__file__).resolve()
LT013G = HERE.parents[1]
LINEAGE = LT013G.parents[2]
PROJECT = Path("/home/debian/Projekty/samsung")
VENDOR = LINEAGE / "vendor/samsung"
LEGACY = LT013G / "gps-v195"
REFERENCE = PROJECT / "reference"
OTA = (PROJECT / "artifacts/SM-T311-G333-ATOMIC-ENFORCING-OTA-20261016" /
       "SM-T311-G333-G301-TO-G333-BOOT-GNSS-ENFORCING-TWRP-SIGNED.zip")
INVENTORIES = [
    ("vendor/samsung/lt013g/proprietary", VENDOR / "lt013g/proprietary"),
    ("vendor/samsung/lt01-common/proprietary", VENDOR / "lt01-common/proprietary"),
    ("device/samsung/lt013g/gps-v195/immutable", LEGACY / "immutable"),
    ("device/samsung/lt013g/radio/prebuilt", LT013G / "radio/prebuilt"),
    ("device/samsung/lt013g/prebuilt", LT013G / "prebuilt"),
]


def sha256(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for piece in iter(lambda: f.read(1024 * 1024), b""):
            h.update(piece)
    return h.hexdigest()


def tracked_set(dirpath):
    proc = subprocess.run(["git", "-C", str(dirpath), "ls-files", "-z"],
                          capture_output=True, check=True)
    return set(proc.stdout.decode("utf-8", "surrogateescape").split("\0"))


def kind(rel):
    x = rel.lower()
    if "gps" in x or "sensor" in x or "orientation" in x or "geomagnetic" in x:
        return "GNSS/sensors"
    if "wifi" in x or "bcm4334" in x or "brcm" in x or "regulatory" in x:
        return "Wi-Fi/Bluetooth/regulatory"
    if "ril" in x or "/cbd" in x:
        return "modem/RIL"
    if "drm" in x or "widevine" in x or "wv" in x.lower():
        return "DRM"
    if "camera" in x or "isp" in x or "setfile" in x:
        return "camera/ISP"
    if "mali" in x or "gles" in x or "egl" in x:
        return "GPU"
    if "mfc" in x or "media" in x or "codec" in x:
        return "video/codec"
    if "/system/lib/" in x or "/system/bin/" in x:
        return "legacy binary/Android shared library"
    return "other firmware/config/legacy data"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    if not OTA.is_file():
        raise SystemExit("The exact staged G333 ZIP is unavailable")

    device_tracked = tracked_set(LT013G)
    result = {
        "device": "Samsung SM-T311 (lt013g)",
        "basis": "G333 staged but not installed; physical live remains G301",
        "purpose": "Internal provenance/inventory only; NOT a redistribution authorization",
        "groups": {},
        "assets": [],
        "unknown_rights_assets": [],
        "staged_update": {},
    }
    for label, root in INVENTORIES:
        if not root.is_dir():
            raise SystemExit(f"Missing required asset tree: {root}")
        files = sorted(p for p in root.rglob("*") if p.is_file() and not p.is_symlink())
        n_tracked = 0
        for f in files:
            relative = f.relative_to(root).as_posix()
            device_rel = f"{label.removeprefix('device/samsung/lt013g/')}/{relative}"
            is_tracked = (label.startswith("device/") and device_rel in device_tracked)
            n_tracked += int(is_tracked)
            data = {
                "inventory_group": label,
                "path": str(f.relative_to(LINEAGE)),
                "type": kind(relative),
                "bytes": f.stat().st_size,
                "sha256": sha256(f),
                "tracked_in_device_git": is_tracked,
                "redistribution_permission": "NOT VERIFIED",
            }
            result["assets"].append(data)
            result["unknown_rights_assets"].append(data["path"])
        local_licenses = [
            p.relative_to(root).as_posix() for p in root.rglob("*")
            if p.is_file() and any(n in p.name.lower()
                                   for n in ("license", "copying", "notice", "eula"))
        ]
        result["groups"][label] = {
            "file_count": len(files),
            "bytes": sum(x.stat().st_size for x in files),
            "tracked_in_device_git": n_tracked,
            "local_license_evidence_files": sorted(local_licenses),
        }

    stock_rootfs = LEGACY / "legacy-rootfs.tar"
    if not stock_rootfs.is_file():
        raise SystemExit("Original legacy-rootfs.tar absent")
    with tarfile.open(stock_rootfs, "r") as tf:
        entries = sorted(x.name for x in tf.getmembers() if x.isfile())
    result["original_rootfs"] = {
        "path": "device/samsung/lt013g/gps-v195/legacy-rootfs.tar",
        "bytes": stock_rootfs.stat().st_size,
        "sha256": sha256(stock_rootfs),
        "file_entries": len(entries),
        "tracked_in_device_git": "gps-v195/legacy-rootfs.tar" in device_tracked,
        "distribution_license_verified": False,
    }
    original_immutable_count = result["groups"]["device/samsung/lt013g/gps-v195/immutable"]["file_count"]
    assert original_immutable_count > 30
    assert len(result["assets"]) >= 130
    # Compare the exact private archive and modem blobs to the checksums
    # already established in the device's public source-only instructions.
    instructions = (LT013G / "PRIVATE_VENDOR_INPUTS.md").read_text()
    assert sha256(stock_rootfs) in instructions, "Private GNSS archive hash drift"
    radio = [a for a in result["assets"]
             if a["inventory_group"].endswith("radio/prebuilt")]
    assert len(radio) == 5
    for item in radio:
        assert item["sha256"] in instructions, ("Unexpected radio blob hash", item["path"])
        assert not item["tracked_in_device_git"], item["path"]
    for asset in result["assets"]:
        if asset["inventory_group"].startswith("device/samsung/lt013g/"):
            assert not asset["tracked_in_device_git"], asset["path"]

    with zipfile.ZipFile(OTA) as z:
        names = z.namelist()
        payloads = sorted(n for n in names if n.startswith("payload/"))
        six = [n for n in payloads if n.startswith("payload/SYSTEM/") or n == "payload/ROOT/sepolicy"]
        result["staged_update"] = {
            "path": str(OTA.relative_to(PROJECT)),
            "bytes": OTA.stat().st_size,
            "sha256": sha256(OTA),
            "all_payload_names": payloads,
            "system_and_policy_entries": len(six),
            "boot_entries": sum("BOOT-G333.img" in n for n in payloads),
            "contains_full_vendor_blob_inventory": False,
            "redistribution_license_verified": False,
        }
    assert len(six) == 6
    assert result["staged_update"]["boot_entries"] == 1
    assert result["original_rootfs"]["tracked_in_device_git"] is False
    assert not result["groups"]["device/samsung/lt013g/gps-v195/immutable"]["tracked_in_device_git"]

    result["observations"] = [
        "Vendor files present on Debian are NOT git repositories; all 48 legacy immutable files, five modem prebuilt files and the local recovery image are excluded from the device Git repository.",
        "A source-only GitHub commit omits binary payloads required to reproduce working GNSS/cellular/graphics; follow PRIVATE_VENDOR_INPUTS.md for verified local checksums/extraction; re-extraction must be done only from lawfully obtained originals.",
        "G333 TWRP installer updates only six GNSS/SELinux system payloads and BOOT; it does not embed the entire original vendor asset inventory, but redistribution of its BOOT/GPS binaries still needs ordinary source/notice/license checks.",
        "A file's name, binary format, Samsung provenance or mere availability does NOT establish redistribution rights. No license grant was proven for opaque OEM blobs by this audit.",
        "GPS immutable rootfs includes some Android/Linux-related libraries that MAY be governed by multiple open licenses. Verify each corresponding upstream notice/source separately; do not label all 48 as legally proprietary.",
        "Do not upload proprietary blobs, stock rootfs tar, or complete firmware images publicly without provenance and license review.",
    ]
    out = args.output.resolve()
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
    print("AUDIT PASS groups", json.dumps(result["groups"], sort_keys=True))
    print("AUDIT PASS original rootfs entries", len(entries), "tracked", result["original_rootfs"]["tracked_in_device_git"])
    print("AUDIT PASS signed OTA payload", payloads, "sha256", result["staged_update"]["sha256"])
    print("AUDIT OUTPUT", out)
    print("RELEASE RIGHTS NOT VERIFIED: keep P0-5 open")


if __name__ == "__main__":
    main()
