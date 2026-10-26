import argparse
import gzip
import hashlib
import json
import subprocess
import sys
from pathlib import Path

EXPECTED_BOOT = "81b01abb5612a9a777330705c02c4bc1f49386da4d5df0950f955246f9fa6bce"
EXPECTED_RECOVERY = "f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f"
EXPECTED_ZIP = "b6db182c721758a3a1ee00eab32ddf40ce6458c54728f918945bef7a3ccd31f6"
EXPECTED_RAW = "ea9ad056e22a912224dd24d09f986d1e0e514459297fce20bbd51eb16ff3fff8"
EXPECTED_TOOL = "8f00756af12f1cbfefa015f88a2c1566659da29554cab5d611a597f66af351ab"
PART_SIZE = 536870912
TOTAL_SIZE = 2235564032
REMOTE = "/data/local/tmp/SM-T311-G563-IO64-CHUNK"

def file_sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for block in iter(lambda: file.read(1048576), b""):
            digest.update(block)
    return digest.hexdigest()


parser = argparse.ArgumentParser()
parser.add_argument("artifact_dir", type=Path)
parser.add_argument("--serial", default="")
parser.add_argument("--flash-token", default="")
parser.add_argument("--resume-from", type=int, choices=range(5), default=0)
args = parser.parse_args()
folder = args.artifact_dir.resolve()
manifest = json.loads((folder / "manifest.json").read_text())
if manifest["targetfiles_sha256"] != EXPECTED_ZIP or manifest["raw_system_sha256"] != EXPECTED_RAW:
    sys.exit("BLOCKED: wrong release hash")
if manifest["device"] != "SM-T311" or manifest["raw_system_bytes"] != TOTAL_SIZE:
    sys.exit("BLOCKED: wrong device or partition size")
parts = manifest["parts"]
if len(parts) != 5 or sum(x["raw_bytes"] for x in parts) != TOTAL_SIZE:
    sys.exit("BLOCKED: incomplete fragment set")
if file_sha256(folder / "t311_io64_chunk") != EXPECTED_TOOL:
    sys.exit("BLOCKED: unexpected recovery helper")
whole_system = hashlib.sha256()
for i, part in enumerate(parts):
    if part["part"] != i or part["offset"] != i * PART_SIZE:
        sys.exit("BLOCKED: unexpected partition offsets")
    if part["gzip_name"] != f"chunk{i}.raw.gz":
        sys.exit("BLOCKED: unexpected gzip filename")
    if part["raw_bytes"] != min(PART_SIZE, TOTAL_SIZE - i * PART_SIZE):
        sys.exit("BLOCKED: unexpected chunk size")
    raw_hash = hashlib.sha256()
    count = 0
    with gzip.open(folder / part["gzip_name"], "rb") as src:
        for data in iter(lambda: src.read(1048576), b""):
            count += len(data)
            raw_hash.update(data)
            whole_system.update(data)
    if raw_hash.hexdigest() != part["raw_sha256"] or count != part["raw_bytes"]:
        sys.exit(f"BLOCKED: invalid part {i} raw SHA/size")
    got_gzip = file_sha256(folder / part["gzip_name"])
    if got_gzip != part["gzip_sha256"]:
        sys.exit(f"BLOCKED: invalid compressed part {i}")
if whole_system.hexdigest() != EXPECTED_RAW:
    sys.exit("BLOCKED: concatenated chunks do not match complete ROM G562 SYSTEM")
print("G563_HOST_ALL_PARTS_PASS", flush=True)
if not args.flash_token:
    print("G563_DRY_RUN_ONLY: no tablet access or partition writes")
    sys.exit(0)
if args.flash_token != "I_AUTHORIZE_G563_RAW_SYSTEM_P20" or not args.serial:
    sys.exit("BLOCKED: physical flashing requires exact token and ADB USB serial")
if ":" in args.serial or "." in args.serial:
    sys.exit("BLOCKED: recovery install requires local USB serial, not network ADB")

def adb(*arguments):
    command = ["adb", "-s", args.serial, *arguments]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError("ADB failed: " + " ".join(arguments) + "\n" + result.stderr)
    return result.stdout.strip()

def remote(command):
    return adb("shell", command)

if adb("get-state") != "device":
    sys.exit("BLOCKED: expected USB device")
recovery = remote("getprop ro.twrp.version")
if recovery != "3.1.1-0":
    sys.exit(f"BLOCKED: original TWRP3.1.1-0 required, actual {recovery!r}")
if remote("id -u") != "0":
    sys.exit("BLOCKED: root recovery shell required")
if remote("blockdev --getsize64 /dev/block/mmcblk0p20") != str(TOTAL_SIZE):
    sys.exit("BLOCKED: wrong SYSTEM geometry")
for block, expected in ((9, EXPECTED_BOOT), (10, EXPECTED_RECOVERY)):
    actual = remote(f"sha256sum /dev/block/mmcblk0p{block}").split()[0]
    if actual != expected:
        sys.exit(f"BLOCKED: p{block} SHA mismatch")
free_line = remote("df -k /data").splitlines()[-1]
free = int(free_line.split()[3]) * 1024
required = max(x["raw_bytes"] + x["gzip_bytes"] for x in parts) + 150 * 1024 * 1024
if free < required:
    sys.exit(f"BLOCKED: DATA space {free} bytes, required {required}")
remote(f"mkdir -p {REMOTE}")
adb("push", str(folder / "t311_io64_chunk"), REMOTE + "/t311_io64_chunk")
remote(f"chmod 700 {REMOTE}/t311_io64_chunk")
if "G563_IO64_PREFLIGHT_PASS" not in remote(f"{REMOTE}/t311_io64_chunk preflight"):
    sys.exit("BLOCKED: SYSTEM mounted or recovery preflight failed")
print("G563_P20_PREFLIGHT_PASS", flush=True)

for part in parts[:args.resume_from]:
    result = remote(f"{REMOTE}/t311_io64_chunk verify-installed "
                    f"{part['part']} {part['raw_sha256']}")
    if f"G564_IO64_INSTALLED_PART_VERIFIED index={part['part']}" not in result:
        sys.exit(f"BLOCKED: cannot resume, existing p20 part {part['part']} differs")
    print(f"G564_RESUME_VERIFIED_PART={part['part']}", flush=True)

system_write_started = False
try:
    for part in parts[args.resume_from:]:
        i = part["part"]
        name = part["gzip_name"]
        adb("push", str(folder / name), REMOTE + "/" + name)
        remote_hash = remote(f"sha256sum {REMOTE}/{name}").split()[0]
        if remote_hash != part["gzip_sha256"]:
            sys.exit(f"BLOCKED: transfer checksum for part {i}")
        remote(f"gzip -dc {REMOTE}/{name} > {REMOTE}/chunk{i}.raw")
        system_write_started = True
        result = remote(f"{REMOTE}/t311_io64_chunk copy {i} "
                        f"{part['raw_sha256']} G563_FLASH_SYSTEM_P20")
        if f"G563_IO64_PART_WRITE_VERIFY_PASS index={i}" not in result:
            sys.exit(f"BLOCKED: write/verify part {i}")
        result = remote(f"{REMOTE}/t311_io64_chunk verify-installed "
                        f"{i} {part['raw_sha256']}")
        if f"G564_IO64_INSTALLED_PART_VERIFIED index={i}" not in result:
            sys.exit(f"BLOCKED: independent verify part {i}")
        remote(f"rm -f {REMOTE}/{name} {REMOTE}/chunk{i}.raw")
        print(f"G563_P20_PART_{i}_PASS", flush=True)

    actual_raw = remote("sync; sha256sum /dev/block/mmcblk0p20").split()[0]
    if actual_raw != EXPECTED_RAW:
        sys.exit("CRITICAL: full SYSTEM SHA wrong. DO NOT REBOOT; remain in recovery")
    print("G563_FULL_SYSTEM_SHA_PASS - release installed, NO AUTOMATIC REBOOT")
except BaseException:
    if system_write_started:
        print("CRITICAL: possible partial SYSTEM update. DO NOT REBOOT. "
              "Stay in TWRP and resume only after fixing the failure.", file=sys.stderr)
    raise
