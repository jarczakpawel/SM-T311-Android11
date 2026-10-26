#!/usr/bin/env python3
import hashlib
import json
import subprocess
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo

ROOT = Path(__file__).resolve().parents[4]
PRODUCT = ROOT / "out/target/product/lt013g/system/system_ext/priv-app/SystemUI"
PROJECT = Path("/home/debian/Projekty/samsung/linux-7.3-port")
ARTIFACTS = PROJECT / "artifacts/G744-RECT-SCREENRECORD-TWRP-20261021"
ARTIFACTS.mkdir(parents=True, exist_ok=True)
REFERENCE = PROJECT / "artifacts/G739-WAKEUP-SEPOLICY-TWRP-20261021/SM-T311-G739-WAKEUP-POLICY-SIGNED.zip"
UNSIGNED = ARTIFACTS / "SM-T311-G744-RECT-SCREENRECORD-UNSIGNED.zip"
SIGNED = ARTIFACTS / "SM-T311-G744-RECT-SCREENRECORD-SIGNED.zip"
SYSTEMUI_PATH = "/system/system/system_ext/priv-app/SystemUI"
OLD = {
    "SystemUI.apk": "0972fe34983d8f5183b4609b64bc8faf109ce335dc4b1febc66802ddf5ab26e5",
    "oat/arm/SystemUI.odex": "a71d1b0e88b8bac1bb80ec6d6ab703d54411b3e252189bca3f793a3d6ab703d54411b3e252189bca3f793a3d6b237098",
    "oat/arm/SystemUI.vdex": "43d85e3807a1f784c21340a9527aa0ac572aeba0fbd6ce8dc92ec3195726b9b4"
}
BOOT = "e53ad50d43e8aa92beccf537b5d2c9cc955f91d8fe2ff64d76a7f174b311177b"
RECOVERY = "f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f"
POLICY = "0ad8562df73cee96f84c68927a07709c62bbb75f70082599ee5cff562d722d3e"
CERT_FINGERPRINT = "C8:A2:E9:BC:CF:59:7C:2F:B6:DC:66:BE:E2:93:FC:13:F2:FC:47:EC:77:BC:6B:2B:0D:52:C1:1F:51:19:2A:B8"

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

NEW = {}
for name in OLD:
    path = PRODUCT / name
    assert path.is_file(), name
    NEW[name] = sha(path)
    assert OLD[name] != NEW[name], name

with ZipFile(PRODUCT / "SystemUI.apk") as apk:
    assert apk.testzip() is None
    assert "classes.dex" in apk.namelist()
    cert = apk.read("META-INF/CERT.RSA")
    cp = subprocess.run(
        ["openssl", "pkcs7", "-inform", "DER", "-print_certs"],
        input=cert, capture_output=True, check=True
    )
    certinfo = subprocess.run(
        ["openssl", "x509", "-noout", "-fingerprint", "-sha256"],
        input=cp.stdout, capture_output=True, check=True
    ).stdout.decode()
    assert CERT_FINGERPRINT in certinfo, certinfo

prepost = [
    "#!/sbin/sh",
    "set -eu",
    "bb=/sbin/busybox",
    'hash() { "$bb" sha256sum "$1" | "$bb" cut -d" " -f1; }',
    'fail() { echo "G744 RECT RECORD $1 REFUSED"; exit "$2"; }',
    f'[ "$(hash /dev/block/mmcblk0p9)" = "{BOOT}" ] || fail BOOT 60',
    f'[ "$(hash /dev/block/mmcblk0p10)" = "{RECOVERY}" ] || fail TWRP 61',
    f'[ "$(hash /system/sepolicy)" = "{POLICY}" ] || fail POLICY 62',
    '[ -f /system/system/build.prop ] || fail SYSTEM_LAYOUT 63',
    'grep -q "^ro.build.version.sdk=30$" /system/system/build.prop || fail ANDROID11 64',
    'case "$1" in',
    "pre)"
]
for index, (name, old) in enumerate(OLD.items()):
    prepost.append(f' [ "$(hash {SYSTEMUI_PATH}/{name})" = "{old}" ] || fail PRE_{index} {65+index}')
prepost.extend([" ;;", "verify)"])
for index, (name, value) in enumerate(NEW.items()):
    prepost.append(f' [ "$(hash {SYSTEMUI_PATH}/{name}.g744-new)" = "{value}" ] || fail STAGED_{index} {70+index}')
prepost.extend([" ;;", "activate)"])
for index, name in enumerate(NEW):
    prepost.append(f' "$bb" mv -f "{SYSTEMUI_PATH}/{name}.g744-new" "{SYSTEMUI_PATH}/{name}" || fail INSTALL_{index} {75+index}')
prepost.extend([" ;;", "post)"])
for index, (name, value) in enumerate(NEW.items()):
    prepost.append(f' [ "$(hash {SYSTEMUI_PATH}/{name})" = "{value}" ] || fail POST_{index} {80+index}')
prepost.extend([' "$bb" sync', ' ;;', "*) exit 90 ;;", "esac"])

edify = [
    'ui_print("G744 SM-T311 rectangular H264 screen recorder with rotation");',
    'ui_print("Only SystemUI APK/OAT/VDEX. Keep BOOT RIL GPS USB DATA unchanged");',
    'assert(getprop("ro.product.device") == "lt013g" || getprop("ro.build.product") == "lt013g" || file_getprop("/default.prop","ro.product.device") == "lt013g");',
    'ifelse(is_mounted("/system"),unmount("/system"),ui_print("Mount system"));',
    'assert(mount("ext4","EMMC","/dev/block/mmcblk0p20","/system","rw"));',
    'assert(package_extract_file("META-INF/g744-guard.sh","/tmp/g744-guard.sh"));',
    'assert(run_program("/sbin/sh","/tmp/g744-guard.sh","pre") == "0");'
]
for name in NEW:
    target = f"{SYSTEMUI_PATH}/{name}.g744-new"
    edify += [
        f'assert(package_extract_file("payload/{name}","{target}"));',
        f'set_metadata("{target}","uid",0,"gid",0,"mode",0644,"selabel","u:object_r:system_file:s0");'
    ]
edify.extend([
    'assert(run_program("/sbin/sh","/tmp/g744-guard.sh","verify") == "0");',
    'assert(run_program("/sbin/sh","/tmp/g744-guard.sh","activate") == "0");',
    'assert(run_program("/sbin/sh","/tmp/g744-guard.sh","post") == "0");',
    'unmount("/system");',
    'ui_print("G744 installed. No format or BOOT/RECOVERY writes");'
])
assert all(x not in "\n".join(edify).lower() for x in ("format(", "delete_recursive(", "wipe("))
with ZipFile(REFERENCE) as ref, ZipFile(UNSIGNED, "w", ZIP_DEFLATED) as z:
    for name in ("META-INF/com/google/android/update-binary", "META-INF/com/android/otacert"):
        z.writestr(ref.getinfo(name), ref.read(name))
    for name, value in (
        ("META-INF/com/google/android/updater-script", "\n".join(edify)+"\n"),
        ("META-INF/g744-guard.sh", "\n".join(prepost)+"\n")
    ):
        item = ZipInfo(name)
        item.compress_type = ZIP_DEFLATED
        item.external_attr = 0o100755 << 16
        z.writestr(item, value)
    for name in NEW:
        z.write(PRODUCT / name, f"payload/{name}")
subprocess.run([
    "java", "-Xmx512m",
    "-Djava.library.path=" + str(ROOT / "out/host/linux-x86/lib64"),
    "-jar", str(ROOT / "out/host/linux-x86/framework/signapk.jar"), "-w",
    str(ROOT / "build/target/product/security/testkey.x509.pem"),
    str(ROOT / "build/target/product/security/testkey.pk8"),
    str(UNSIGNED), str(SIGNED)
], check=True)
with ZipFile(SIGNED) as z:
    assert z.testzip() is None
    for name, value in NEW.items():
        assert hashlib.sha256(z.read(f"payload/{name}")).hexdigest() == value
manifest = {
    "status": "STAGED_NOT_INSTALLED",
    "zip": str(SIGNED), "sha256": sha(SIGNED), "bytes": SIGNED.stat().st_size,
    "old": OLD, "new": NEW, "boot_unchanged": True, "data_unchanged": True,
    "framework_commit": "b085b6b6dff", "device_commit": "13ba2db"
}
(ARTIFACTS / "G744_TWRP_MANIFEST.json").write_text(json.dumps(manifest, indent=2) + "\n")
print(json.dumps(manifest, indent=2))
