#!/usr/bin/env python3
import hashlib
import json
import subprocess
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo

ROOT = Path(__file__).resolve().parents[4]
PRODUCT = ROOT / "out/target/product/lt013g/system/priv-app/MediaProvider"
PROJECT = Path("/home/debian/Projekty/samsung/linux-7.3-port")
ARTIFACTS = PROJECT / "artifacts/G753-MEDIAPROVIDER-QUOTA-TWRP-20261021"
ARTIFACTS.mkdir(parents=True, exist_ok=True)
REFERENCE = PROJECT / "artifacts/G739-WAKEUP-SEPOLICY-TWRP-20261021/SM-T311-G739-WAKEUP-POLICY-SIGNED.zip"
UNSIGNED = ARTIFACTS / "SM-T311-G753-MEDIAPROVIDER-UNSIGNED.zip"
SIGNED = ARTIFACTS / "SM-T311-G753-MEDIAPROVIDER-SIGNED.zip"
SYSTEMUI_PATH = "/system/system/priv-app/MediaProvider"
OLD = {
    "MediaProvider.apk": "9b2497f6e57a74aac135db03eed5c39cd591d6bfccfc97fa6528c0cb33ab1d59",
    "oat/arm/MediaProvider.odex": "7eb66de90b24e2f5eb2526c55305c70996fdda26471518911275fba83eeeec52",
    "oat/arm/MediaProvider.vdex": "129a660c4fbe11dbdd17d9c36afbc22d6d805fbc2437a55f0047e813d9343193"
}
BOOT = "e53ad50d43e8aa92beccf537b5d2c9cc955f91d8fe2ff64d76a7f174b311177b"
RECOVERY = "f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f"
POLICY = "c4fbbaa869be6dec2aba7bb559cda5d563335c5c81821ea9aa7f36234404e7b1"
CERT_FINGERPRINT = "46:59:83:F7:79:1F:2A:BE:B4:3E:A2:CB:DC:7F:21:A8:26:0B:72:BC:08:A5:5C:83:9F:C1:A4:3B:C7:41:A8:1E"

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

NEW = {}
for name in OLD:
    path = PRODUCT / name
    assert path.is_file(), name
    NEW[name] = sha(path)
    assert OLD[name] != NEW[name], name

with ZipFile(PRODUCT / "MediaProvider.apk") as apk:
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
    'fail() { echo "G753 SCREENREC $1 REFUSED"; exit "$2"; }',
    f'[ "$(hash /dev/block/mmcblk0p9)" = "{BOOT}" ] || fail BOOT 60',
    f'[ "$(hash /dev/block/mmcblk0p10)" = "{RECOVERY}" ] || fail TWRP 61',
    f'[ "$(hash /system/sepolicy)" = "{POLICY}" ] || fail POLICY 62',
    '[ "$(hash /system/system/system_ext/priv-app/SystemUI/SystemUI.apk)" = "ae50cd2f0298eb32ccbe82f85781a888618ce834ba534fe5571d7c363be41a3e" ] || fail G752_SYSTEMUI 88',
    '[ "$(hash /system/system/lib/libcodec2_hidl@1.0.so)" = "77aab570ea9ceddc4d0f7fa0e50852bb8dd5c067e965ec090d188c01eaf21510" ] || fail G751_CODEC2 89',
    '[ "$(hash /system/system/lib/libcameraservice.so)" = "276c70c85b2665659b609871bca22f399ac6223d7366d0fc12090c4ef1729538" ] || fail G749_CAMERA 90',
    '[ "$(hash /system/system/vendor/etc/selinux/vendor_sepolicy.cil)" = "c8f95c2fbf9eed9dca03f6465942ca7ea8258496a6475b532f9f5538e0603434" ] || fail G750_SEPOLICY_CIL 91',
    '[ -f /system/system/build.prop ] || fail SYSTEM_LAYOUT 63',
    'grep -q "^ro.build.version.sdk=30$" /system/system/build.prop || fail ANDROID11 64',
    'case "$1" in',
    "pre)"
]
for index, (name, old) in enumerate(OLD.items()):
    prepost.append(f' [ "$(hash {SYSTEMUI_PATH}/{name})" = "{old}" ] || fail PRE_{index} {65+index}')
prepost.extend([" ;;", "verify)"])
for index, (name, value) in enumerate(NEW.items()):
    prepost.append(f' [ "$(hash {SYSTEMUI_PATH}/{name}.g753-new)" = "{value}" ] || fail STAGED_{index} {70+index}')
prepost.extend([" ;;", "activate)"])
for index, name in enumerate(NEW):
    prepost.append(f' "$bb" mv -f "{SYSTEMUI_PATH}/{name}.g753-new" "{SYSTEMUI_PATH}/{name}" || fail INSTALL_{index} {75+index}')
prepost.extend([" ;;", "post)"])
for index, (name, value) in enumerate(NEW.items()):
    prepost.append(f' [ "$(hash {SYSTEMUI_PATH}/{name})" = "{value}" ] || fail POST_{index} {80+index}')
prepost.extend([' "$bb" sync', ' ;;', "*) exit 90 ;;", "esac"])

edify = [
    'ui_print("G753 SM-T311 MediaProvider no unused project quota ioctl");',
    'ui_print("Only MediaProvider APK/OAT/VDEX. Keep BOOT RIL GPS USB DATA unchanged");',
    'assert(getprop("ro.product.device") == "lt013g" || getprop("ro.build.product") == "lt013g" || file_getprop("/default.prop","ro.product.device") == "lt013g");',
    'ifelse(is_mounted("/system"),unmount("/system"),ui_print("Mount system"));',
    'assert(mount("ext4","EMMC","/dev/block/mmcblk0p20","/system","rw"));',
    'assert(package_extract_file("META-INF/g753-guard.sh","/tmp/g753-guard.sh"));',
    'assert(run_program("/sbin/sh","/tmp/g753-guard.sh","pre") == "0");'
]
for name in NEW:
    target = f"{SYSTEMUI_PATH}/{name}.g753-new"
    edify += [
        f'assert(package_extract_file("payload/{name}","{target}"));',
        f'set_metadata("{target}","uid",0,"gid",0,"mode",0644,"selabel","u:object_r:system_file:s0");'
    ]
edify.extend([
    'assert(run_program("/sbin/sh","/tmp/g753-guard.sh","verify") == "0");',
    'assert(run_program("/sbin/sh","/tmp/g753-guard.sh","activate") == "0");',
    'assert(run_program("/sbin/sh","/tmp/g753-guard.sh","post") == "0");',
    'unmount("/system");',
    'ui_print("G753 MediaProvider installed. No BOOT/RECOVERY changes");'
])
assert all(x not in "\n".join(edify).lower() for x in ("format(", "delete_recursive(", "wipe("))
with ZipFile(REFERENCE) as ref, ZipFile(UNSIGNED, "w", ZIP_DEFLATED) as z:
    for name in ("META-INF/com/google/android/update-binary", "META-INF/com/android/otacert"):
        z.writestr(ref.getinfo(name), ref.read(name))
    for name, value in (
        ("META-INF/com/google/android/updater-script", "\n".join(edify)+"\n"),
        ("META-INF/g753-guard.sh", "\n".join(prepost)+"\n")
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
    "framework_commit": "8f02ae56", "device_commit": "8710134"
}
(ARTIFACTS / "G753_TWRP_MANIFEST.json").write_text(json.dumps(manifest, indent=2) + "\n")
print(json.dumps(manifest, indent=2))
