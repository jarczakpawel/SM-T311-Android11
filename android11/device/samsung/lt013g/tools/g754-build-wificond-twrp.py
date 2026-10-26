#!/usr/bin/env python3
"""SM-T311 Wificond Netlink nested attribute handling: guarded one-executable TWRP delta."""
import hashlib
import json
import subprocess
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED, ZipInfo

root = Path(__file__).resolve().parents[4]
project = Path("/home/debian/Projekty/samsung")
out = project / "artifacts/G754-WIFICOND-NLA7-TWRP-20261021"
out.mkdir(parents=True, exist_ok=True)
source = root / "out/target/product/lt013g/system/bin/wificond"
previous = project / "linux-7.3-port/artifacts/G748-MEDIA-MEMFD-SELINUX-TWRP-20261021/SM-T311-G748-MEDIA-MEMFD-POLICY-SIGNED.zip"
dest = "/system/system/bin/wificond"
old_sha = "8e4b2fe2fa8b129e688b5ff9b1fd14fc7bbd0983754afb3728c1f5af96cf4006"
boot_sha = "e53ad50d43e8aa92beccf537b5d2c9cc955f91d8fe2ff64d76a7f174b311177b"
twrp_sha = "f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f"
sepolicy_sha = "c4fbbaa869be6dec2aba7bb559cda5d563335c5c81821ea9aa7f36234404e7b1"
sysui_sha = "ae50cd2f0298eb32ccbe82f85781a888618ce834ba534fe5571d7c363be41a3e"
def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()
new_sha = sha(source)
assert new_sha == "5606d985a8a37384e6830fd763497115a6d8b32a69f56d1aa34ac320f0ed82b4", new_sha
with source.open("rb") as f:
    assert f.read(4) == b"\x7fELF"
guard = "\n".join([
    "#!/sbin/sh", "set -eu",
    'h() { /sbin/busybox sha256sum "$1" | /sbin/busybox cut -d" " -f1; }',
    'bad() { echo "G754 REFUSED $1"; exit "$2"; }',
    f'[ "$(h /dev/block/mmcblk0p9)" = "{boot_sha}" ] || bad BOOT 60',
    f'[ "$(h /dev/block/mmcblk0p10)" = "{twrp_sha}" ] || bad RECOVERY 61',
    f'[ "$(h /system/sepolicy)" = "{sepolicy_sha}" ] || bad POLICY 62',
    '[ "$(h /system/system/vendor/etc/selinux/vendor_sepolicy.cil)" = "c8f95c2fbf9eed9dca03f6465942ca7ea8258496a6475b532f9f5538e0603434" ] || bad G750_VENDOR_CIL 71',
    '[ "$(h /system/system/lib/libcameraservice.so)" = "276c70c85b2665659b609871bca22f399ac6223d7366d0fc12090c4ef1729538" ] || bad G749_CAMERA 72',
    f'[ "$(h /system/system/system_ext/priv-app/SystemUI/SystemUI.apk)" = "{sysui_sha}" ] || bad SYSTEMUI 63',
    '[ "$(h /system/system/priv-app/MediaProvider/MediaProvider.apk)" = "de4aa973de7787e11bbc99b9bfc4e3934d7d26b14ce605b470657cc21ca862f1" ] || bad G753_MEDIAPROVIDER 81',
    '[ "$(h /system/system/lib/libcodec2_hidl@1.0.so)" = "77aab570ea9ceddc4d0f7fa0e50852bb8dd5c067e965ec090d188c01eaf21510" ] || bad G751_CODEC2 82',
    '[ -f /system/system/build.prop ] || bad LAYOUT 64',
    'grep -q "^ro.build.version.sdk=30$" /system/system/build.prop || bad SDK 65',
    'case "$1" in',
    f'  pre) [ "$(h {dest})" = "{old_sha}" ] || bad OLD_LIB 66 ;;',
    f'  staged) [ "$(h {dest}.g754-new)" = "{new_sha}" ] || bad NEW_LIB 67 ;;',
    f'  install) /sbin/busybox mv -f "{dest}.g754-new" "{dest}" || bad MOVE 68 ;;',
    f'  post) [ "$(h {dest})" = "{new_sha}" ] || bad POST_LIB 69; /sbin/busybox sync ;;',
    '  *) bad INVALID_ACTION 70 ;;',
    'esac', ''
])
edify = [
    'ui_print("G754 SM-T311 kernel 7.3 nested Netlink attribute handling for WiFi");',
    'ui_print("Only wificond. BOOT/RECOVERY/G749-G753 preserved");',
    'assert(getprop("ro.product.device") == "lt013g" || getprop("ro.build.product") == "lt013g" || file_getprop("/default.prop","ro.product.device") == "lt013g");',
    'ifelse(is_mounted("/system"),unmount("/system"),ui_print("Mount system"));',
    'assert(mount("ext4","EMMC","/dev/block/mmcblk0p20","/system","rw"));',
    'assert(package_extract_file("META-INF/g754-guard.sh","/tmp/g754-guard.sh"));',
    'assert(run_program("/sbin/sh","/tmp/g754-guard.sh","pre") == "0");',
    f'assert(package_extract_file("payload/wificond","{dest}.g754-new"));',
    f'set_metadata("{dest}.g754-new","uid",0,"gid",2000,"mode",0755,"selabel","u:object_r:wificond_exec:s0");',
    'assert(run_program("/sbin/sh","/tmp/g754-guard.sh","staged") == "0");',
    'assert(run_program("/sbin/sh","/tmp/g754-guard.sh","install") == "0");',
    'assert(run_program("/sbin/sh","/tmp/g754-guard.sh","post") == "0");',
    'unmount("/system");',
    'ui_print("G754 installed; check real WiFi RSSI and link speed");'
]
assert not any(x in "\n".join(edify) for x in ("format(", "delete_recursive(", "wipe("))
unsigned = out / "SM-T311-G754-WIFICOND-UNSIGNED.zip"
signed = out / "SM-T311-G754-WIFICOND-SIGNED.zip"
with ZipFile(previous) as baseline, ZipFile(unsigned, "w", ZIP_DEFLATED) as z:
    for name in ("META-INF/com/google/android/update-binary", "META-INF/com/android/otacert"):
        z.writestr(baseline.getinfo(name), baseline.read(name))
    for name, contents in (
        ("META-INF/com/google/android/updater-script", "\n".join(edify)+"\n"),
        ("META-INF/g754-guard.sh", guard)
    ):
        info=ZipInfo(name)
        info.compress_type=ZIP_DEFLATED
        info.external_attr=0o100755 << 16
        z.writestr(info, contents)
    z.write(source,"payload/wificond")
subprocess.run(["java","-Xmx512m",
    "-Djava.library.path="+str(root/"out/host/linux-x86/lib64"),
    "-jar",str(root/"out/host/linux-x86/framework/signapk.jar"),"-w",
    str(root/"build/target/product/security/testkey.x509.pem"),
    str(root/"build/target/product/security/testkey.pk8"),
    str(unsigned),str(signed)],check=True)
with ZipFile(signed) as z:
    assert z.testzip() is None
    assert hashlib.sha256(z.read("payload/wificond")).hexdigest()==new_sha
meta={"signed_zip":str(signed),"signed_zip_sha256":sha(signed),"new_wificond_sha256":new_sha,
      "old_wificond_sha256":old_sha, "status":"STAGED_NOT_INSTALLED","source_commit":"0865848"}
(out/"G754_TWRP_MANIFEST.json").write_text(json.dumps(meta,indent=2)+"\n")
print(json.dumps(meta,indent=2))
