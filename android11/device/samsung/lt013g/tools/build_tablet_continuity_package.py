#!/usr/bin/env python3
"""Export the current private SM-T311 state plus indexed conversations.

This is a tablet-side continuity mirror, not a replacement for Git,
the Debian project directory, or a FLASH/OTA installation. Contains private
chat exports; NEVER publish or place it in public/shared external storage.
"""
import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED

p = argparse.ArgumentParser()
p.add_argument("--project", required=True, type=Path)
p.add_argument("--android", required=True, type=Path)
p.add_argument("--kernel", required=True, type=Path)
p.add_argument("--output", required=True, type=Path)
args = p.parse_args()
project, android, kernel, output = args.project, args.android, args.kernel, args.output
history = project / "chat-history"
index = (history / "INDEX.md").read_text()
# INDEX uses backtick-delimited paths including filenames with spaces.
arc_re = re.compile(r"exports/([^`]+\.md)")
expected = {"exports/" + m.group(1) for m in arc_re.finditer(index)}
assert len(expected) >= 40, f"Unexpectedly truncated history index: {len(expected)}"

files = {}
for f in sorted(history.rglob("*")):
    if f.is_file() and f.is_relative_to(history / "exports"):
        rel = "exports/" + f.name
        if rel not in expected and rel.removesuffix(".json") + ".md" not in expected:
            continue
    if f.is_file():
        files["chat-history/" + str(f.relative_to(history))] = f

for rel in sorted(expected):
    assert "chat-history/" + rel in files, f"Missing indexed chat export {rel}"
# Only pre-redacted/no-coordinate GPS diagnostics are mirrored; NEVER raw
# logcat, dumpsys location, AGPS addresses, NMEA, RIL identities or user data.
gps_artifacts = project / "artifacts/SM-T311-G385-GPS-FIRST-20261016"
for basename in (
    "G385-GPS-READONLY-RUNTIME.txt",
    "G385-GPS-STOCK-LIVE-GATE.json",
    "G385-GNSS-ACTIVE-70S-PHYSICAL.txt",
    "G385-GNSS-ACTIVE-RESULT.json",
    "G385-GNSS-CNO-70S-PHYSICAL.txt",
    "G385-GNSS-CNO-RESULT.json",
):
    file = gps_artifacts / basename
    if file.is_file():
        files["safely-redacted-g385-gps/" + basename] = file

# G390 standalone TWRP read-only preflight and real old-recovery offline
# evidence, without repacking or replacing unchanged G389 signed OTA.
g390_artifacts = project / "artifacts/SM-T311-G390-TWRP-PREFLIGHT-20261017"
for basename in ("G390-READONLY-PREFLIGHT-SOURCE-GATE.json",
                 "G390-FINAL-DEVICE-STATE.txt"):
    f = g390_artifacts / basename
    if f.is_file():
        files["g390-old-twrp-readonly-safety/" + basename] = f

# G389 SELinux enforcing stock OTA p11 label; preserve concise release
# evidence, never duplicate the huge signed OTA in private history.
g389_artifacts = project / "artifacts/SM-T311-G389-ENFORCING-BCB-20261017"
for basename in ("G389-ENFORCING-BCB-RELEASE-GATE.json",
                 "G389-GUARDED-OTA-MANIFEST.json",
                 "G389-SYSTEM-BYTE-GATE.json",
                 "G389-FINAL-DEVICE-STATE.txt"):
    f = g389_artifacts / basename
    if f.is_file():
        files["g389-enforcing-bcb-source-evidence/" + basename] = f

# G388 recovery BCB stock PIT OTA p11 read-only evidence, cumulative
# release checks, and source instructions; do not archive old huge OTA ZIPs.
g388_artifacts = project / "artifacts/SM-T311-G388-RECOVERY-BCB-20261017"
for basename in ("G388-PHYSICAL-FSTAB-BCB-PARITY.txt",
                 "G388-BCB-RELEASE-GATE.json",
                 "G388-GUARDED-OTA-MANIFEST.json",
                 "G388-SYSTEM-BYTE-GATE.json",
                 "G388-FINAL-DEVICE-STATE.txt"):
    f=g388_artifacts / basename
    if f.is_file():
        files["g388-recovery-bcb-source-evidence/" + basename] = f

# G387 includes only bounded compile/route/release proof, never the 484MB
# signed firmware in an already private 195MB conversation/source archive.
g387_artifacts = project / "artifacts/SM-T311-G387-XMM6262-ROUTE-20261017"
for basename in ("G387-SOURCE-RELEASE-GATE.json",
                 "G387-GUARDED-OTA-MANIFEST.json",
                 "G387-SYSTEM-BYTE-GATE.json",
                 "G387-xmm6262-route-target-files-fstab.json",
                 "G387-FINAL-DEVICE-STATE.txt",
                 "mke2fs-aosp-1.45.conf"):
    f=g387_artifacts / basename
    if f.is_file():
        files["g387-source-release-evidence/" + basename] = f

# G386 observations explicitly omit IP addresses, coordinates, raw modem/APN
# output and unredacted logcat. Only export source-verified bounded fields.
g386_artifacts = project / "artifacts/SM-T311-G386-GPS-NTP-20261017"
for basename in ("G386-SAFE-PHYSICAL-ASSISTANCE.txt",
                 "G386-NTP-ROUTING-RESULT.json"):
    file = g386_artifacts / basename
    if file.is_file():
        files["safely-redacted-g386-ntp/" + basename] = file

for relative in [
    "device/samsung/lt013g/docs/SM-T311-STOCK-FIRST-HARDWARE-PARITY-G383.md",
    "device/samsung/lt013g/docs/G384-STOCK-HALL-MUIC-PHYSICAL-IRQ.md",
    "device/samsung/lt013g/docs/G385-AUTONOMOUS-GPS-FIRST-PRIORITY-MATRIX.md",
    "device/samsung/lt013g/docs/G385-GPS-BCM4752-UART-IDLE-PHYSICAL-AUDIT.md",
    "device/samsung/lt013g/docs/G386-GPS-ASSISTANCE-NTP-NETWORK-ROUTING.md",
    "device/samsung/lt013g/tools/test_g386_gps_assistance_routing.py",
    "device/samsung/lt013g/docs/G387-XMM6262-PDP32-GATEWAY-ROUTE.md",
    "device/samsung/lt013g/tools/test_g387_xmm6262_ipv4_host_route.py",
    "device/samsung/lt013g/tools/stage_g387_xmm6262_route_target_files.py",
    "device/samsung/lt013g/tools/test_g388_recovery_bcb_fstab.py",
    "device/samsung/lt013g/tools/stage_g388_recovery_bcb_target_files.py",
    "device/samsung/lt013g/docs/G388-ANDROID11-RECOVERY-BCB-OTA-P11.md",
    "device/samsung/lt013g/tools/test_g389_bcb_enforcing_selinux.py",
    "device/samsung/lt013g/tools/stage_g389_enforcing_bcb_target_files.py",
    "device/samsung/lt013g/docs/G389-ENFORCING-OTA-P11-MISC-BCB-ACCESS.md",
    "device/samsung/lt013g/tools/g390_twrp_readonly_preflight.sh",
    "device/samsung/lt013g/tools/test_g390_twrp_preflight_offline.py",
    "device/samsung/lt013g/docs/G390-REAL-TWRP-G389-READONLY-PREFLIGHT.md",
    "device/samsung/lt013g/tools/t311-ntp-probe/Android.bp",
    "device/samsung/lt013g/tools/t311-ntp-probe/ntp_probe.c",
    "device/samsung/lt013g/tools/test_g385_gps_stock_uart_idle_contract.py",
    "device/samsung/lt013g/tools/analyze_g385_gnss_satellite_run.py",
    "device/samsung/lt013g/tools/t311-gnss-status/src/org/pjarczak/t311gnssstatus/GnssProbeService.java",
    "device/samsung/lt013g/tools/test_g384_hall_pmic_runtime_contract.py",
    "device/samsung/lt013g/docs/SM-T311-HARDWARE-FUNCTION-MATRIX.md",
    "device/samsung/lt013g/docs/G382-PRIVATE-GPS-BINDER-SELINUX.md",
    "device/samsung/lt013g/tools/test_g383_stock_sm_t311_contract.py",
    "device/samsung/lt013g/tools/test_g382_gps_private_binder_policy.py",
]:
    f = android / relative
    if f.is_file():
        files["source/" + relative] = f

repo_specs = [
    ("device-lt013g", android / "device/samsung/lt013g", "c3eb194^"),
    ("device-lt01-common", android / "device/samsung/lt01-common", "919d83e^"),
    ("frameworks-av", android / "frameworks/av", "c0b4aae^"),
    ("frameworks-opt-telephony", android / "frameworks/opt/telephony", "740e44d3d^"),
    ("kernel-g378-binder", kernel, "2c69912ec^"),
]
patches = {}
heads = {}
for name, cwd, base in repo_specs:
    heads[name] = subprocess.check_output(["git", "-C", str(cwd), "rev-parse", "HEAD"], text=True).strip()
    patch = subprocess.check_output(["git", "-C", str(cwd), "format-patch", "--stdout", f"{base}..HEAD"])
    assert patch, f"Empty source patch {name}"
    assert len(patch) < 20_000_000, f"Unexpected patch size {name}"
    patches["git-source-patches/" + name + ".mbox"] = patch
    if name == "frameworks-opt-telephony":
        # Preserve the user's 4 pre-existing UNCOMMITTED Android telephony
        # working-tree edits alongside our separately committed G387 route fix.
        # Do not stage/commit unrelated work or pretend it is included in HEAD.
        dirty = subprocess.check_output(
            ["git", "-C", str(cwd), "diff", "--binary", "HEAD", "--",
             "src/java/com/android/internal/telephony/RIL.java",
             "src/java/com/android/internal/telephony/RadioResponse.java",
             "src/java/com/android/internal/telephony/dataconnection/DataConnection.java",
             "src/java/com/android/internal/telephony/uicc/SIMRecords.java"])
        if dirty:
            assert len(dirty) < 500_000, "Unexpected large or unsafe telephony diff"
            patches["git-source-patches/" + name + "-UNCOMMITTED-TRACKED-DIFF.patch"] = dirty

# Every indexed snapshot (including older MD-only) and matching JSON is inside.
assert sum(k.endswith(".md") and k.startswith("chat-history/exports/") for k in files) == len(expected)
meta = {"schema": 1, "type": "PRIVATE_TABLET_PROJECT_CONTINUITY",
        "flash_or_update_performed": False,
        "conversation_md_indexed_count": len(expected),
        "conversation_json_count": sum(k.startswith("chat-history/exports/") and k.endswith(".json") for k in files),
        "git_heads": heads,
        "latest_installed_android": "G379",
        "latest_staged_guarded_ota": "G389",
        "latest_staged_ota_sha256": "339d7e2ac87cfdacd6957c92450fe2a503902ac334c6c53204f07474f344c09b",
        "notes": "Private conversations; device root-only storage, not a public distributable.",
        "entries": {}}
output.parent.mkdir(parents=True, exist_ok=True)
with ZipFile(output, "w", compression=ZIP_DEFLATED, compresslevel=2, allowZip64=True) as z:
    for arc, file in sorted(files.items()):
        raw = file.read_bytes()
        meta["entries"][arc] = {"bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}
        z.writestr(arc, raw)
    for arc, raw in sorted(patches.items()):
        meta["entries"][arc] = {"bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}
        z.writestr(arc, raw)
    z.writestr("MANIFEST.json", json.dumps(meta, ensure_ascii=False, indent=2) + "\n")
with ZipFile(output) as z:
    assert z.testzip() is None
    actual = json.loads(z.read("MANIFEST.json"))
    assert actual["conversation_md_indexed_count"] == len(expected)
    assert len(actual["entries"]) == len(files) + len(patches)
sha = hashlib.file_digest(output.open("rb"), "sha256").hexdigest()
report = {"status": "PASS", "output": str(output), "bytes": output.stat().st_size,
          "sha256": sha, "conversation_md": len(expected),
          "conversation_json": meta["conversation_json_count"],
          "entries": len(meta["entries"]), "git_heads": heads,
          "sensitive": "private conversation archive - keep /data/local/tmp mode 0600",
          "installed_rom": "G379", "new_rom_staged_only": "G389"}
output.with_suffix(".json").write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n")
print(json.dumps(report, indent=2, ensure_ascii=False))
