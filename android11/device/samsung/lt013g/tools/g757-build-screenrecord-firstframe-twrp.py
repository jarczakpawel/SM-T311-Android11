#!/usr/bin/env python3
import hashlib
import json
import subprocess
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo

ROOT = Path(__file__).resolve().parents[4]
PRODUCT = ROOT / "out/target/product/lt013g/system/system_ext/priv-app/SystemUI"
PROJECT = Path("/home/debian/Projekty/samsung/linux-7.3-port")
ARTIFACTS = PROJECT / "artifacts/G757-SCREENRECORD-FIRSTFRAME-TWRP-20261021"
ARTIFACTS.mkdir(parents=True, exist_ok=True)
REFERENCE = PROJECT / "artifacts/G739-WAKEUP-SEPOLICY-TWRP-20261021/SM-T311-G739-WAKEUP-POLICY-SIGNED.zip"
UNSIGNED = ARTIFACTS / "SM-T311-G757-FIRSTFRAME-UNSIGNED.zip"
SIGNED = ARTIFACTS / "SM-T311-G757-FIRSTFRAME-SIGNED.zip"
SYSTEMUI_PATH = "/system/system/system_ext/priv-app/SystemUI"
OLD = {
    "SystemUI.apk": "ae50cd2f0298eb32ccbe82f85781a888618ce834ba534fe5571d7c363be41a3e",
    "oat/arm/SystemUI.odex": "5dd23b1f88026b490e6a4eaf7092554c3277051f93dc55f1907338f9b4135e7d",
    "oat/arm/SystemUI.vdex": "4ed98507106409bd7bcb6881243a52e5256991a942cbdb11aeb510500aa74d02"
}
BOOT = "e53ad50d43e8aa92beccf537b5d2c9cc955f91d8fe2ff64d76a7f174b311177b"
RECOVERY = "f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f"
POLICY = "914b5b3eec5b3210628c250d0173bcef53716b38990009a5a9f5a039fff9483b"
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
    'fail() { echo "G757 SCREENREC $1 REFUSED"; exit "$2"; }',
    f'[ "$(hash /dev/block/mmcblk0p9)" = "{BOOT}" ] || fail BOOT 60',
    f'[ "$(hash /dev/block/mmcblk0p10)" = "{RECOVERY}" ] || fail TWRP 61',
    f'[ "$(hash /system/sepolicy)" = "{POLICY}" ] || fail POLICY 62',
    '[ "$(hash /system/system/lib/libcodec2_hidl@1.0.so)" = "77aab570ea9ceddc4d0f7fa0e50852bb8dd5c067e965ec090d188c01eaf21510" ] || fail G751_CODEC2 89',
    '[ "$(hash /system/system/lib/libcameraservice.so)" = "276c70c85b2665659b609871bca22f399ac6223d7366d0fc12090c4ef1729538" ] || fail G749_CAMERA 90',
    '[ "$(hash /system/system/priv-app/MediaProvider/MediaProvider.apk)" = "de4aa973de7787e11bbc99b9bfc4e3934d7d26b14ce605b470657cc21ca862f1" ] || fail G753_MEDIA 92',
    '[ "$(hash /system/system/bin/wificond)" = "5606d985a8a37384e6830fd763497115a6d8b32a69f56d1aa34ac320f0ed82b4" ] || fail G754_WIFI 93',
    '[ "$(hash /system/system/vendor/etc/selinux/vendor_sepolicy.cil)" = "2d496899f9f560d0130423b9aa41bf335c77df7ad2b2fe003e579357f6bca0ce" ] || fail G750_SEPOLICY_CIL 91',
    '[ -f /system/system/build.prop ] || fail SYSTEM_LAYOUT 63',
    'grep -q "^ro.build.version.sdk=30$" /system/system/build.prop || fail ANDROID11 64',
    'case "$1" in',
    "pre)"
]
for index, (name, old) in enumerate(OLD.items()):
    prepost.append(f' [ "$(hash {SYSTEMUI_PATH}/{name})" = "{old}" ] || fail PRE_{index} {65+index}')
prepost.extend([" ;;", "verify)"])
for index, (name, value) in enumerate(NEW.items()):
    prepost.append(f' [ "$(hash {SYSTEMUI_PATH}/{name}.g757-new)" = "{value}" ] || fail STAGED_{index} {70+index}')
prepost.extend([" ;;", "activate)"])
for index, name in enumerate(NEW):
    prepost.append(f' "$bb" mv -f "{SYSTEMUI_PATH}/{name}.g757-new" "{SYSTEMUI_PATH}/{name}" || fail INSTALL_{index} {75+index}')
prepost.extend([" ;;", "post)"])
for index, (name, value) in enumerate(NEW.items()):
    prepost.append(f' [ "$(hash {SYSTEMUI_PATH}/{name})" = "{value}" ] || fail POST_{index} {80+index}')
prepost.extend([' "$bb" sync', ' ;;', "*) exit 90 ;;", "esac"])

edify = [
    'ui_print("G757 SM-T311 connect screen mirror only after hardware AVC encoder starts");',
    'ui_print("Only SystemUI APK/OAT/VDEX. Keep BOOT RIL GPS USB DATA unchanged");',
    'assert(getprop("ro.product.device") == "lt013g" || getprop("ro.build.product") == "lt013g" || file_getprop("/default.prop","ro.product.device") == "lt013g");',
    'ifelse(is_mounted("/system"),unmount("/system"),ui_print("Mount system"));',
    'assert(mount("ext4","EMMC","/dev/block/mmcblk0p20","/system","rw"));',
    'assert(package_extract_file("META-INF/g757-guard.sh","/tmp/g757-guard.sh"));',
    'assert(run_program("/sbin/sh","/tmp/g757-guard.sh","pre") == "0");'
]
for name in NEW:
    target = f"{SYSTEMUI_PATH}/{name}.g757-new"
    edify += [
        f'assert(package_extract_file("payload/{name}","{target}"));',
        f'set_metadata("{target}","uid",0,"gid",0,"mode",0644,"selabel","u:object_r:system_file:s0");'
    ]
edify.extend([
    'assert(run_program("/sbin/sh","/tmp/g757-guard.sh","verify") == "0");',
    'assert(run_program("/sbin/sh","/tmp/g757-guard.sh","activate") == "0");',
    'assert(run_program("/sbin/sh","/tmp/g757-guard.sh","post") == "0");',
    'unmount("/system");',
    'ui_print("G757 first-frame recorder installed; BOOT/RECOVERY unchanged");'
])
assert all(x not in "\n".join(edify).lower() for x in ("format(", "delete_recursive(", "wipe("))
with ZipFile(REFERENCE) as ref, ZipFile(UNSIGNED, "w", ZIP_DEFLATED) as z:
    for name in ("META-INF/com/google/android/update-binary", "META-INF/com/android/otacert"):
        z.writestr(ref.getinfo(name), ref.read(name))
    for name, value in (
        ("META-INF/com/google/android/updater-script", "\n".join(edify)+"\n"),
        ("META-INF/g757-guard.sh", "\n".join(prepost)+"\n")
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
    "framework_commit": "3d5b0a28374", "device_commit": "4331c9f"
}
(ARTIFACTS / "G757_TWRP_MANIFEST.json").write_text(json.dumps(manifest, indent=2) + "\n")
print(json.dumps(manifest, indent=2))
