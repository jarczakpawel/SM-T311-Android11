#!/usr/bin/env python3
"""SM-T311 read-only release audit: target-files ZIP versus actual live SHA snapshot.

Never touches Android: consumes a previously captured read-only SHA256 inventory.
This gate detects source/release drift after hot-installed packages, including
SystemUI ART preopt mismatches, RIL/Samsung OEM library mismatches and HAL drift.
"""
import argparse
import hashlib
import io
import json
import re
import struct
import subprocess
import tempfile
from pathlib import Path
from zipfile import ZipFile

SHA_LINE = re.compile(r"^([0-9a-f]{64})  (/[^\n\r\0]+)$")
CONTRACT = {
    "/vendor/lib/libril.so",
    "/vendor/lib/t311-ril/libsec-ril.so",
    "/vendor/lib/hw/gps.default.so",
    "/sepolicy",
    "/system/system_ext/priv-app/SystemUI/SystemUI.apk",
    "/system/system_ext/priv-app/SystemUI/oat/arm/SystemUI.odex",
    "/system/system_ext/priv-app/SystemUI/oat/arm/SystemUI.vdex",
}
UI_BASE = "SYSTEM/system_ext/priv-app/SystemUI/"
UI_APK = UI_BASE + "SystemUI.apk"
UI_ODEX = UI_BASE + "oat/arm/SystemUI.odex"
UI_VDEX = UI_BASE + "oat/arm/SystemUI.vdex"

def sha_stream(handle):
    h = hashlib.sha256()
    while True:
        block = handle.read(1024 * 1024)
        if not block:
            break
        h.update(block)
    return h.hexdigest()

def parse_live(path):
    result = {}
    for line in path.read_text().splitlines():
        m = SHA_LINE.fullmatch(line)
        if not m:
            continue
        digest, name = m.groups()
        if ".." in Path(name).parts:
            raise ValueError("Invalid live path")
        if name in result:
            raise ValueError("Duplicate live SHA " + name)
        result[name] = digest
    if not CONTRACT.issubset(result):
        raise ValueError("Incomplete live snapshot: " + repr(sorted(CONTRACT.difference(result))))
    return result

def mapping(name):
    if name == "/sepolicy":
        return ("BOOT/RAMDISK/sepolicy", "SYSTEM/sepolicy", "ROOT/sepolicy")
    if name.startswith("/system/"):
        return ("SYSTEM/" + name.removeprefix("/system/"),)
    if name.startswith("/vendor/"):
        return ("SYSTEM/vendor/" + name.removeprefix("/vendor/"),
                "VENDOR/" + name.removeprefix("/vendor/"))
    return ()

def vdex_crc(vdex):
    if len(vdex) < 28 or vdex[:8] != b"vdex021\x00":
        raise ValueError("Unsupported VDEX header")
    n = struct.unpack_from("<I", vdex, 12)[0]
    if not 1 <= n <= 32 or len(vdex) < 28 + 4*n:
        raise ValueError("Invalid VDEX DEX count")
    return list(struct.unpack_from("<" + "I"*n, vdex, 28))

def dex_crc(apk_bytes):
    with ZipFile(io.BytesIO(apk_bytes)) as app:
        dex = []
        for name in app.namelist():
            if name == "classes.dex":
                dex.append((1, app.getinfo(name).CRC))
            elif name.startswith("classes") and name.endswith(".dex"):
                suffix = name[len("classes"):-len(".dex")]
                if suffix.isdecimal() and int(suffix) >= 2:
                    dex.append((int(suffix), app.getinfo(name).CRC))
        dex.sort()
        if not dex or [index for index, _ in dex] != list(range(1,len(dex)+1)):
            raise ValueError("No contiguous DEX entries")
        return [crc for _,crc in dex]

def boot_prefix_gate(z, present, expected_sha256):
    if expected_sha256 is None:
        return {"status":"SKIP"}
    if not re.fullmatch(r"[0-9a-f]{64}", expected_sha256):
        raise ValueError("Invalid expected boot checksum")
    name = "IMAGES/boot.img"
    if name not in present:
        return {"status":"MISSING"}
    with z.open(name) as handle:
        data = handle.read(3339 * 2048)
    if len(data) != 3339 * 2048:
        return {"status":"INVALID_SIZE","bytes":len(data)}
    checksum = hashlib.sha256(data).hexdigest()
    return {"status":"MATCH" if checksum == expected_sha256 else "DIFFERENT",
            "target_sha256":checksum,"expected_live_sha256":expected_sha256,
            "prefix_bytes":len(data)}

def enforcing_gate(z, present, analyzer):
    if analyzer is None:
        return {"status":"SKIP"}
    policy = next((name for name in ("ROOT/sepolicy","BOOT/RAMDISK/sepolicy","SYSTEM/sepolicy")
                   if name in present), None)
    if policy is None:
        return {"status":"MISSING"}
    with tempfile.TemporaryDirectory(prefix="smt311-policy-") as directory:
        policy_file=Path(directory)/"sepolicy"
        policy_file.write_bytes(z.read(policy))
        cmd=subprocess.run([str(analyzer),str(policy_file),"permissive"],
                           capture_output=True,text=True,timeout=35)
    if cmd.returncode != 0:
        return {"status":"ANALYZER_ERROR","detail":cmd.stderr[-300:]}
    domains=sorted(set(cmd.stdout.split()))
    return {"status":"PASS" if not domains else "BLOCK",
            "permissive_domains":domains,"policy_zip_entry":policy}

def check(target_files, live_file, boot_expected=None, policy_analyzer=None):
    live = parse_live(live_file)
    with ZipFile(target_files) as z:
        names = z.namelist()
        if len(names) != len(set(names)):
            raise ValueError("Duplicate ZIP paths")
        present = set(names)
        data = {}
        for path,digest in sorted(live.items()):
            matches = [p for p in mapping(path) if p in present]
            if len(matches) > 1:
                raise ValueError("Multiple target locations for "+path)
            if not matches:
                data[path] = {"status":"MISSING","live_sha256":digest,"in_target_files":None}
                continue
            with z.open(matches[0]) as fd:
                built = sha_stream(fd)
            data[path] = {
                "status": "MATCH" if built == digest else "DIFFERENT",
                "live_sha256":digest, "zip_sha256":built, "in_target_files":matches[0]}
        ui = {"status":"UNKNOWN"}
        if UI_APK not in present:
            ui = {"status":"NO_APK"}
        elif UI_VDEX not in present or UI_ODEX not in present:
            ui = {"status":"APK_ONLY_OR_INCOMPLETE_PREOPT","odex_present":UI_ODEX in present,
                  "vdex_present":UI_VDEX in present}
        else:
            try:
                apkchecks = dex_crc(z.read(UI_APK))
                vchecks = vdex_crc(z.read(UI_VDEX))
                ui = {"status":"MATCH" if apkchecks == vchecks else "MISMATCH",
                      "apk_dex_crc32":[f"{x:08x}" for x in apkchecks],
                      "vdex_dex_crc32":[f"{x:08x}" for x in vchecks]}
            except (ValueError, KeyError) as exc:
                ui = {"status":"INVALID","reason":str(exc)[:200]}
        boot = boot_prefix_gate(z,present,boot_expected)
        policy = enforcing_gate(z,present,policy_analyzer)
        critical = {k:v["status"] for k,v in data.items() if k in CONTRACT}
        count = {name:sum(v["status"] == name for v in data.values())
                 for name in ("MATCH","DIFFERENT","MISSING")}
        gate = all(value=="MATCH" for value in critical.values()) and ui["status"]=="MATCH" and count["DIFFERENT"]==0 and count["MISSING"]==0
        if boot_expected is not None:
            gate &= boot["status"] == "MATCH"
        if policy_analyzer is not None:
            gate &= policy["status"] == "PASS"
        return {"schema":2,"target_files":str(target_files),"live_snapshot":str(live_file),
                "boot_prefix":boot,"selinux_policy":policy,
                "inventory_count":len(live),"status":"PASS" if gate else "BLOCK",
                "counts":count,"critical":critical,"systemui_preopt":ui,"comparison":data}

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--zip",type=Path,required=True)
    parser.add_argument("--live",type=Path,required=True)
    parser.add_argument("--report",type=Path,required=True)
    parser.add_argument("--boot-expected",help="SHA256 of the deployed first 3339x2048 boot bytes")
    parser.add_argument("--policy-analyzer",type=Path,help="Path to native sepolicy-analyze; blocks permissive domains")
    args=parser.parse_args()
    result=check(args.zip,args.live,args.boot_expected,args.policy_analyzer)
    args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps(result,ensure_ascii=False,indent=2)+"\n")
    print(f"G366_TARGET_FILES_GATE={result['status']} counts={result['counts']} "
          f"SystemUI={result['systemui_preopt']['status']} boot={result['boot_prefix']['status']} "
          f"selinux={result['selinux_policy']['status']} report={args.report}")
    for path,status in sorted(result["critical"].items()):
        print("CRITICAL", status, path)
    return 0 if result["status"]=="PASS" else 2

if __name__=="__main__":
    raise SystemExit(main())
