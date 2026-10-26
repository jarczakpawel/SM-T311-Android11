#!/usr/bin/env python3
"""G387 source release gate: exact ARM32 compiled /32 PDP route + OEM pins.

Does not flash. Refuse if G387 appears unchanged versus G382, if the
bootclasspath ART/OAT/VDEX lacks telephony outputs or immutable factory
BOOT/TWRP/stock RIL/audio/GPS SELinux checks from G382 do not pass.
"""
import argparse
import hashlib
from io import BytesIO
import json
from pathlib import Path
import subprocess
import sys
from zipfile import ZipFile

parser=argparse.ArgumentParser()
parser.add_argument('input',type=Path)
parser.add_argument('staged',type=Path)
parser.add_argument('--baseline',type=Path,required=True)
parser.add_argument('--android',type=Path,required=True)
arg=parser.parse_args()
assert arg.input.resolve()!=arg.staged.resolve()
need=[
    "SYSTEM/framework/telephony-common.jar",
    "SYSTEM/framework/arm/boot-telephony-common.art",
    "SYSTEM/framework/arm/boot-telephony-common.oat",
    "SYSTEM/framework/boot-telephony-common.vdex",
    "SYSTEM/lib/libaudiohal.so",
    "SYSTEM/vendor/lib/libril.so",
    "SYSTEM/vendor/etc/selinux/vendor_sepolicy.cil",
    "IMAGES/boot.img",
    "IMAGES/recovery.img",
]
def dex_contains(jar,token):
    with ZipFile(BytesIO(jar)) as z:
        return token in z.read('classes.dex')
with ZipFile(arg.input) as src, ZipFile(arg.baseline) as base:
    # A diagnostic-only Soong `m <module>` build leaves installed files
    # in out/target/product until explicitly removed. They must not be
    # silently swept into a user ROM's target-files SYSTEM tree.
    forbidden=("SYSTEM/product/app/T311GnssStatus/",
               "SYSTEM/vendor/bin/T311NtpTransportProbe")
    for entry in src.namelist():
        if any(entry.startswith(prefix) for prefix in forbidden):
            raise SystemExit("G387 forbids test-only image payload "+entry)
    for n in need:
        if n not in src.namelist() or n not in base.namelist():
            raise SystemExit("Missing strict cumulative release file "+n)
    method=b"addLt013gPdpGatewayHostRoute"
    current_jar=src.read(need[0])
    baseline_jar=base.read(need[0])
    if not dex_contains(current_jar,method):
        raise SystemExit("G387 route correction missing in compiled Android ARM32 classes.dex")
    if dex_contains(baseline_jar,method):
        raise SystemExit("G382 baseline unexpectedly already contains the G387 method")
    if hashlib.sha256(current_jar).digest()==hashlib.sha256(baseline_jar).digest():
        raise SystemExit("No telephony ART implementation difference between G382 and G387")
    # Exact factory critical binaries and private GNSS Binder SELinux
    for n in need[1:]:
        if n in ("SYSTEM/framework/arm/boot-telephony-common.art",
                 "SYSTEM/framework/arm/boot-telephony-common.oat",
                 "SYSTEM/framework/boot-telephony-common.vdex"):
            if not src.read(n):
                raise SystemExit("G387 empty compiled telephony ART artifact: "+n)
        elif src.read(n)!=base.read(n):
            raise SystemExit("G387 unexpected mutation to OEM/BOOT/RECOVERY/audio/GPS sepolicy: "+n)
    if not dex_contains(current_jar,b"rmnet1") or not dex_contains(current_jar,b"lt013g"):
        raise SystemExit("G387 compiled method guard model/interface strings missing")
    manifest={
        "status":"G387_ARM32_ROUTE_BOOTCLASSPATH_STOCK_PINNED",
        "telephony_jar_sha256":hashlib.sha256(current_jar).hexdigest(),
        "previous_g382_jar_sha256":hashlib.sha256(baseline_jar).hexdigest(),
        "new_helper_in_compiled_dex":True,
        "factory_boot_recovery_ril_audio_and_private_gps_policy_unchanged":True,
        "physical_flash_performed":False,
        "full_g387_ota_staged_only":True,
    }

# Run actual G387 Java extraction tests rather than trusting only strings.
subprocess.run([sys.executable,
                str(Path(__file__).with_name("test_g387_xmm6262_ipv4_host_route.py")),
                "--android",str(arg.android)], check=True)
# Reuse byte-for-byte G382 policy + immutable BOOT/RIL/TWRP/Audio6 VINTF release checks.
subprocess.run([sys.executable,
                str(Path(__file__).with_name("stage_g382_binder_audio_target_files.py")),
                str(arg.input),str(arg.staged)],check=True)
with ZipFile(arg.staged) as staged, ZipFile(arg.input) as source_again:
    if staged.read(need[0]) != current_jar:
        raise SystemExit("G387 compiler dex mutated during fstab staging")
    for n in need[1:]:
        if staged.read(n)!=source_again.read(n):
            raise SystemExit("G387 critical payload changed during staging: "+n)
manifest["staged_target_files_sha256"]=hashlib.file_digest(arg.staged.open('rb'),'sha256').hexdigest()
manifest["staged_target_files_bytes"]=arg.staged.stat().st_size
arg.staged.with_name("G387-SOURCE-RELEASE-GATE.json").write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps(manifest,indent=2))
