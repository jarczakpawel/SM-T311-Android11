#!/usr/bin/env python3
# G774 dedicated physical deployment method for G773 MMS candidate.
# Never write BOOT/RECOVERY/DATA; no backups, no call/audio/GPS tests.
# Source G772 QA is candidate-only and not proof of physical boot.
import argparse
import gzip
import hashlib
import json
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
SYSTEM = ROOT / "out/target/product/lt013g/system.img"
PARTITION_BYTES = 2235564032
CHUNK_BYTES = 536870912
HELPER_SHA = "ba964378166bb4263487f85da7ae86df1f597853edde27b892f98acd789c6ff0"
BOOT_SHA = "e53ad50d43e8aa92beccf537b5d2c9cc955f91d8fe2ff64d76a7f174b311177b"
RECOVERY_SHA = "f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f"
REMOTE = "/data/local/tmp/SM-T311-G563-IO64-CHUNK"


def sha(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for data in iter(lambda: stream.read(1048576), b""):
            digest.update(data)
    return digest.hexdigest()


parser = argparse.ArgumentParser()
parser.add_argument("release", type=Path)
parser.add_argument("--serial", required=True)
parser.add_argument("--flash-token", default="")
parser.add_argument("--resume-from", type=int, choices=range(5), default=0)
args = parser.parse_args()
folder = args.release.resolve()
manifest = json.loads((folder / "manifest.json").read_text())
qa = json.loads((folder / "IMAGE_QA.json").read_text())
if (manifest.get("device") != "SM-T311"
        or manifest.get("stage") != "G773_MMS_SECURITY_SYSTEM"
        or manifest.get("result") != "VERIFIED_CANDIDATE_NOT_INSTALLED"
        or manifest.get("image_sha256") != "bd417f0d0f941b6fc7200ac5164da8fea6029d728d7f478871df28c02e95fab3"
        or manifest.get("part_count") != 5
        or manifest.get("raw_system_bytes") != PARTITION_BYTES
        or manifest.get("chunk_bytes") != CHUNK_BYTES
        or manifest.get("writer_sha256") != HELPER_SHA
        or qa.get("result") != "CANDIDATE_PASS_NOT_INSTALLED" or qa.get("candidate_only") is not True or qa.get("hardware_boot_acceptance") is not False
        or qa.get("verified_file_count") != 17
        or qa.get("filesystem_clean") is not True
        or qa.get("image_sha256") != "bd417f0d0f941b6fc7200ac5164da8fea6029d728d7f478871df28c02e95fab3"
        or qa.get("system_partition_bytes") != PARTITION_BYTES
        or qa.get("image_sha256") != manifest.get("image_sha256")
        or Path(qa.get("source", "")).resolve() != SYSTEM.resolve()):
    sys.exit("G774 BLOCKED: unqualified system release")
if not SYSTEM.is_file() or sha(SYSTEM) != manifest["image_sha256"]:
    sys.exit("G774 BLOCKED: current source image changed")
if sha(folder / "t311_io64_chunk") != HELPER_SHA:
    sys.exit("G774 BLOCKED: writer SHA mismatch")
parts = manifest["parts"]
if len(parts) != 5:
    sys.exit("G774 BLOCKED: missing partitions")
whole = hashlib.sha256()
for i, part in enumerate(parts):
    offset = i * CHUNK_BYTES
    length = min(CHUNK_BYTES, PARTITION_BYTES - offset)
    compressed = folder / f"chunk{i}.raw.gz"
    if (part.get("part") != i
            or part.get("offset") != offset
            or part.get("raw_bytes") != length
            or part.get("gzip_name") != compressed.name
            or compressed.stat().st_size != part.get("gzip_bytes")
            or sha(compressed) != part.get("gzip_sha256")):
        sys.exit(f"G774 BLOCKED: chunk {i} manifest mismatch")
    digest = hashlib.sha256()
    count = 0
    with gzip.open(compressed, "rb") as source:
        for data in iter(lambda: source.read(1048576), b""):
            digest.update(data)
            whole.update(data)
            count += len(data)
    if digest.hexdigest() != part["raw_sha256"] or count != length:
        sys.exit(f"G774 BLOCKED: compressed chunk {i} content mismatch")
if whole.hexdigest() != manifest["raw_system_sha256"]:
    sys.exit("G774 BLOCKED: raw system SHA mismatch")
print("G774 ALL RELEASE PARTS VERIFIED, NO DEVICE WRITES", flush=True)
if args.flash_token != "G774_MMS_SYSTEM_P20":
    sys.exit(0)
if not args.serial or ":" in args.serial or "." in args.serial:
    sys.exit("G774 BLOCKED: physical USB serial required")


def adb(*command, timeout=150):
    result = subprocess.run(["adb", "-s", args.serial, *command],
                            capture_output=True, text=True, timeout=timeout)
    if result.returncode != 0:
        raise RuntimeError("ADB command failed: " + " ".join(command)
                           + "\n" + result.stdout + "\n" + result.stderr)
    return result.stdout.strip()


def remote(command, timeout=150):
    return adb("shell", command, timeout=timeout)


if adb("get-state") != "device":
    sys.exit("G774 BLOCKED: USB device unavailable")
if remote("getprop ro.twrp.version") != "3.1.1-0":
    adb("reboot", "recovery")
    adb("wait-for-device", timeout=180)
for attempt in range(30):
    if remote("getprop ro.twrp.version") == "3.1.1-0":
        break
    time.sleep(1)
else:
    sys.exit("G774 BLOCKED: TWRP 3.1.1-0 did not start")
if remote("id -u") != "0":
    sys.exit("G774 BLOCKED: recovery shell not root")
if remote("blockdev --getsize64 /dev/block/mmcblk0p20") != str(PARTITION_BYTES):
    sys.exit("G774 BLOCKED: wrong SYSTEM partition")
for part, expected in ((9, BOOT_SHA), (10, RECOVERY_SHA)):
    if remote(f"sha256sum /dev/block/mmcblk0p{part}").split()[0] != expected:
        sys.exit(f"G774 BLOCKED: p{part} image changed")
free = int(remote("df -k /data").splitlines()[-1].split()[3]) * 1024
required = max(p["raw_bytes"] + p["gzip_bytes"] for p in parts) + 268435456
if free < required:
    sys.exit("G774 BLOCKED: insufficient recovery DATA space")
if remote(f"sha256sum {REMOTE}/t311_io64_chunk").split()[0] != HELPER_SHA:
    sys.exit("G774 BLOCKED: active TWRP helper hash mismatch")
if "G570_DATA_MOUNT_VERIFIED" not in remote(f"{REMOTE}/t311_io64_chunk preflight-data"):
    sys.exit("G774 BLOCKED: DATA preflight failed")
if "G563_IO64_PREFLIGHT_PASS" not in remote(f"{REMOTE}/t311_io64_chunk preflight"):
    sys.exit("G774 BLOCKED: SYSTEM mounted or inaccessible")
for part in parts[:args.resume_from]:
    output = remote(f"{REMOTE}/t311_io64_chunk verify-installed "
                    f"{part['part']} {part['raw_sha256']}", timeout=180)
    if f"index={part['part']}" not in output:
        sys.exit("G774 BLOCKED: resume partition mismatch")

started = False
try:
    for part in parts[args.resume_from:]:
        i = part["part"]
        gz = f"{REMOTE}/chunk{i}.raw.gz"
        raw = f"{REMOTE}/chunk{i}.raw"
        adb("push", str(folder / part["gzip_name"]), gz, timeout=300)
        if remote(f"sha256sum {gz}").split()[0] != part["gzip_sha256"]:
            sys.exit(f"G774 BLOCKED: transfer hash {i}")
        remote(f"gzip -dc {gz} > {raw}", timeout=300)
        started = True
        result = remote(f"{REMOTE}/t311_io64_chunk copy {i} "
                        f"{part['raw_sha256']} G563_FLASH_SYSTEM_P20", timeout=300)
        if f"index={i}" not in result or "WRITE_VERIFY_PASS" not in result:
            sys.exit(f"G774 CRITICAL: chunk {i} writer verification failed")
        remote(f"rm -f {gz} {raw}")
        print(f"G774_PART_{i}_WRITE_VERIFIED", flush=True)
    result = remote(f"{REMOTE}/t311_io64_chunk verify-system "
                    f"{manifest['raw_system_sha256']}", timeout=300)
    if "G569_IO64_FULL_SYSTEM_VERIFIED" not in result:
        sys.exit("G774 CRITICAL: full SYSTEM hash mismatch")
    print("G774 COMPLETE SYSTEM VERIFIED; NO BOOT OR RECOVERY WRITES", flush=True)
except BaseException:
    if started:
        print("G774 CRITICAL: stay in TWRP; SYSTEM may be partially updated",
              file=sys.stderr)
    raise
