#!/usr/bin/env python3
"""SM-T311 camera1 25-fps regression: signed, strict-preflight one-file TWRP delta."""
import hashlib
import json
import subprocess
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED, ZipInfo

root = Path(__file__).resolve().parents[4]
project = Path("/home/debian/Projekty/samsung")
out = project / "artifacts/G749-CAMERA25-API1-TWRP-20261021"
out.mkdir(parents=True, exist_ok=True)
source = root / "out/target/product/lt013g/system/lib/libcameraservice.so"
previous = project / "linux-7.3-port/artifacts/G748-MEDIA-MEMFD-SELINUX-TWRP-20261021/SM-T311-G748-MEDIA-MEMFD-POLICY-SIGNED.zip"
dest = "/system/system/lib/libcameraservice.so"
old_sha = "7760377593ffea72d671f22e915fa9083a9f55a0f80da06eb10f461fb5af4e1e"
boot_sha = "e53ad50d43e8aa92beccf537b5d2c9cc955f91d8fe2ff64d76a7f174b311177b"
twrp_sha = "f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f"
sepolicy_sha = "7b2ec921d5837cc44131bf258d8ed3f88ad19630d16e83eb919c08d245fa858f"
sysui_sha = "9903f158b0fe83bc0f28506a9adba7c34433706b85ca313d6f2dceea7f3b4419"
def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()
new_sha = sha(source)
assert new_sha == "276c70c85b2665659b609871bca22f399ac6223d7366d0fc12090c4ef1729538", new_sha
with source.open("rb") as f:
    assert f.read(4) == b"\x7fELF"
guard = "\n".join([
    "#!/sbin/sh", "set -eu",
    'h() { /sbin/busybox sha256sum "$1" | /sbin/busybox cut -d" " -f1; }',
    'bad() { echo "G749 REFUSED $1"; exit "$2"; }',
    f'[ "$(h /dev/block/mmcblk0p9)" = "{boot_sha}" ] || bad BOOT 60',
    f'[ "$(h /dev/block/mmcblk0p10)" = "{twrp_sha}" ] || bad RECOVERY 61',
    f'[ "$(h /system/sepolicy)" = "{sepolicy_sha}" ] || bad POLICY 62',
    f'[ "$(h /system/system/system_ext/priv-app/SystemUI/SystemUI.apk)" = "{sysui_sha}" ] || bad SYSTEMUI 63',
    '[ -f /system/system/build.prop ] || bad LAYOUT 64',
    'grep -q "^ro.build.version.sdk=30$" /system/system/build.prop || bad SDK 65',
    'case "$1" in',
    f'  pre) [ "$(h {dest})" = "{old_sha}" ] || bad OLD_LIB 66 ;;',
    f'  staged) [ "$(h {dest}.g749-new)" = "{new_sha}" ] || bad NEW_LIB 67 ;;',
    f'  install) /sbin/busybox mv -f "{dest}.g749-new" "{dest}" || bad MOVE 68 ;;',
    f'  post) [ "$(h {dest})" = "{new_sha}" ] || bad POST_LIB 69; /sbin/busybox sync ;;',
    '  *) bad INVALID_ACTION 70 ;;',
    'esac', ''
])
edify = [
    'ui_print("G749 SM-T311 front SR130PC20 25fps Camera1 preview regression");',
    'ui_print("Only libcameraservice. No BOOT/RECOVERY/SELinux/CameraHAL modifications");',
    'assert(getprop("ro.product.device") == "lt013g" || getprop("ro.build.product") == "lt013g" || file_getprop("/default.prop","ro.product.device") == "lt013g");',
    'ifelse(is_mounted("/system"),unmount("/system"),ui_print("Mount system"));',
    'assert(mount("ext4","EMMC","/dev/block/mmcblk0p20","/system","rw"));',
    'assert(package_extract_file("META-INF/g749-guard.sh","/tmp/g749-guard.sh"));',
    'assert(run_program("/sbin/sh","/tmp/g749-guard.sh","pre") == "0");',
    f'assert(package_extract_file("payload/libcameraservice.so","{dest}.g749-new"));',
    f'set_metadata("{dest}.g749-new","uid",0,"gid",0,"mode",0644,"selabel","u:object_r:system_lib_file:s0");',
    'assert(run_program("/sbin/sh","/tmp/g749-guard.sh","staged") == "0");',
    'assert(run_program("/sbin/sh","/tmp/g749-guard.sh","install") == "0");',
    'assert(run_program("/sbin/sh","/tmp/g749-guard.sh","post") == "0");',
    'unmount("/system");',
    'ui_print("G749 installed; please check camera with owner after reboot");'
]
assert not any(x in "\n".join(edify) for x in ("format(", "delete_recursive(", "wipe("))
unsigned = out / "SM-T311-G749-CAMERA25-UNSIGNED.zip"
signed = out / "SM-T311-G749-CAMERA25-SIGNED.zip"
with ZipFile(previous) as baseline, ZipFile(unsigned, "w", ZIP_DEFLATED) as z:
    for name in ("META-INF/com/google/android/update-binary", "META-INF/com/android/otacert"):
        z.writestr(baseline.getinfo(name), baseline.read(name))
    for name, contents in (
        ("META-INF/com/google/android/updater-script", "\n".join(edify)+"\n"),
        ("META-INF/g749-guard.sh", guard)
    ):
        info=ZipInfo(name)
        info.compress_type=ZIP_DEFLATED
        info.external_attr=0o100755 << 16
        z.writestr(info, contents)
    z.write(source,"payload/libcameraservice.so")
subprocess.run(["java","-Xmx512m",
    "-Djava.library.path="+str(root/"out/host/linux-x86/lib64"),
    "-jar",str(root/"out/host/linux-x86/framework/signapk.jar"),"-w",
    str(root/"build/target/product/security/testkey.x509.pem"),
    str(root/"build/target/product/security/testkey.pk8"),
    str(unsigned),str(signed)],check=True)
with ZipFile(signed) as z:
    assert z.testzip() is None
    assert hashlib.sha256(z.read("payload/libcameraservice.so")).hexdigest()==new_sha
meta={"signed_zip":str(signed),"signed_zip_sha256":sha(signed),"new_camera_sha256":new_sha,
      "old_camera_sha256":old_sha, "status":"STAGED_NOT_INSTALLED","source_commit":"9f30553b20"}
(out/"G749_TWRP_MANIFEST.json").write_text(json.dumps(meta,indent=2)+"\n")
print(json.dumps(meta,indent=2))
