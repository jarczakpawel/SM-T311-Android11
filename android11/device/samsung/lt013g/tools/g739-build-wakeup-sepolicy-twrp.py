#!/usr/bin/env python3
"""G739 guarded TWRP update for wakeup sysfs SELinux labels.

No firmware/kernel/modem/PIN/GPS changes, no backups or formatting. Package
is built and SHA verified offline; use it only with verified USB ADB and
known working TWRP. Does not execute any install or reboot itself.
"""
import hashlib
import json
import subprocess
from pathlib import Path
from zipfile import ZipFile, ZipInfo, ZIP_DEFLATED

A=Path(__file__).resolve().parents[4]
P=Path("/home/debian/Projekty/samsung/linux-7.3-port")
OUT=P/"artifacts/G739-WAKEUP-SEPOLICY-TWRP-20261021"
OUT.mkdir(parents=True,exist_ok=True)
PREVIOUS=P/"artifacts/G735-OTG-TWRP-OWNER-ATTENDED/SM-T311-G735-OTG-OWNER-TWRP-SIGNED.zip"
UNSIGNED=OUT/"SM-T311-G739-WAKEUP-POLICY-UNSIGNED.zip"
SIGNED=OUT/"SM-T311-G739-WAKEUP-POLICY-SIGNED.zip"
SOURCE=A/"out/target/product/lt013g/system"
FILES={
    "vendor_sepolicy.cil":{
       "source":SOURCE/"vendor/etc/selinux/vendor_sepolicy.cil",
       "target":"/system/system/vendor/etc/selinux/vendor_sepolicy.cil",
       "old":"a3b5dc65cbe28bca151fbfa3a0998b6c4d2262bceb65cf33c5881b8a39fe5dfe",
       "new":"7382e941466e93cf30d3b41ddcc6b3d9474fe004455e097e47c19b88a532fc36",
       "label":"u:object_r:vendor_configs_file:s0",
    },
    "precompiled_sepolicy":{
       "source":SOURCE/"vendor/etc/selinux/precompiled_sepolicy",
       "target":"/system/system/vendor/etc/selinux/precompiled_sepolicy",
       "old":"8263d8529e064a725454d4d0fee57f03c75b951fa576d770f7704b804e1515eb",
       "new":"a7205be08b6e6cc42a6f1e8f04dcc9ca49b90537a59b953673daf58e633039fc",
       "label":"u:object_r:vendor_configs_file:s0",
    },
    "sepolicy":{
       "source":A/"out/target/product/lt013g/root/sepolicy",
       "target":"/system/sepolicy",
       "old":"8263d8529e064a725454d4d0fee57f03c75b951fa576d770f7704b804e1515eb",
       "new":"a7205be08b6e6cc42a6f1e8f04dcc9ca49b90537a59b953673daf58e633039fc",
       "label":"u:object_r:sepolicy_file:s0",
    },
}
BOOT_SHA="e53ad50d43e8aa92beccf537b5d2c9cc955f91d8fe2ff64d76a7f174b311177b"
TWRP_SHA="f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f"
USB_HAL_SHA="fe0cc1bb9ba1caed0603f4cc8175d593af20b1cd585ea69ffaaffb785d9fa075"

def sha(blob):
    return hashlib.sha256(blob).hexdigest()
for name,spec in FILES.items():
    assert spec["source"].exists() and sha(spec["source"].read_bytes())==spec["new"],name
assert PREVIOUS.is_file()

prepost=[
    "#!/sbin/sh","set -eu",'bb=/sbin/busybox',
    'h() { "$bb" sha256sum "$1" | "$bb" cut -d" " -f1; }',
    'bad() { echo "G739 POLICY $1 REFUSED" >&2; exit "$2"; }',
    f'[ "$(h /dev/block/mmcblk0p9)" = "{BOOT_SHA}" ] || bad BOOT_NOT_G730 60',
    f'[ "$(h /dev/block/mmcblk0p10)" = "{TWRP_SHA}" ] || bad TWRP_RECOVERY 61',
    f'[ "$(h /system/system/vendor/bin/hw/android.hardware.usb.gadget@1.1-service)" = "{USB_HAL_SHA}" ] || bad HAL_NOT_G733 62',
    '[ -f /system/system/build.prop ] || bad SYSTEM_LAYOUT 63',
    '[ "$(h /system/system/bin/vold)" = "be0a2c91e4fb9e7dd19e00a2e2db45fa830b5971ceb84eb3dc48129b3491eeb5" ] || bad G738_NOT_INSTALLED 71',
    'grep -q "^ro.build.version.sdk=30$" /system/system/build.prop || bad ANDROID_SDK 64',
    'case "$1" in','pre)',
]
for n,s in FILES.items():
    prepost.append(f' [ "$(h {s["target"]})" = "{s["old"]}" ] || bad PRE_{n.replace(".","_")} 65')
prepost.extend([' ;;','stage)'])
for n,s in FILES.items():
    prepost.append(f' [ "$(h /tmp/g739-{n})" = "{s["new"]}" ] || bad SOURCE_{n.replace(".","_")} 66')
prepost.extend([' ;;','verify_staging)'])
for n,s in FILES.items():
    prepost.append(f' [ "$(h {s["target"]}.g739-new)" = "{s["new"]}" ] || bad STAGING_{n.replace(".","_")} 67')
prepost.extend([' ;;','activate)'])
for n,s in FILES.items():
    prepost.append(f' [ "$(h {s["target"]}.g739-new)" = "{s["new"]}" ] || bad BEFORE_RENAME_{n.replace(".","_")} 68')
for n,s in FILES.items():
    prepost.append(f' "$bb" mv -f {s["target"]}.g739-new {s["target"]} || bad RENAME_{n.replace(".","_")} 69')
prepost.extend([' "$bb" sync',' ;;','post)'])
for n,s in FILES.items():
    prepost.append(f' [ "$(h {s["target"]})" = "{s["new"]}" ] || bad AFTER_{n.replace(".","_")} 70')
prepost.extend([' "$bb" sync',' ;;','*) exit 99;;','esac'])
edify=[
    'ui_print("SM-T311 G739: exact PMIC and modem wakeup SELinux labels");',
    'ui_print("UPDATE ONLY three policies; preserve BOOT, RIL, GPS and userdata");',
    'assert(getprop("ro.product.device") == "lt013g" || getprop("ro.build.product") == "lt013g" || getprop("ro.product.device") == "lt013gxx" || getprop("ro.build.product") == "lt013gxx" || file_getprop("/default.prop","ro.product.device") == "lt013g");',
    'ifelse(is_mounted("/system"),unmount("/system"),ui_print("Mount system"));',
    'assert(mount("ext4","EMMC","/dev/block/mmcblk0p20","/system","rw"));',
    'assert(package_extract_file("META-INF/g739-prepost.sh","/tmp/g739-prepost.sh"));',
    'assert(run_program("/sbin/sh","/tmp/g739-prepost.sh","pre") == "0");',
]
for name in FILES:
    edify.append(f'assert(package_extract_file("payload/{name}","/tmp/g739-{name}"));')
edify.append('assert(run_program("/sbin/sh","/tmp/g739-prepost.sh","stage") == "0");')
for n,s in FILES.items():
    edify.append(f'assert(package_extract_file("payload/{n}","{s["target"]}.g739-new"));')
    edify.append(f'set_metadata("{s["target"]}.g739-new","uid",0,"gid",0,"mode",0644,"selabel","{s["label"]}");')
edify.extend([
    'assert(run_program("/sbin/sh","/tmp/g739-prepost.sh","verify_staging") == "0");',
    'assert(run_program("/sbin/sh","/tmp/g739-prepost.sh","activate") == "0");',
    'assert(run_program("/sbin/sh","/tmp/g739-prepost.sh","post") == "0");',
    'unmount("/system");',
    'ui_print("G739 wakeup SELinux update installed; BOOT/RECOVERY/MODEM unchanged");',
])
with ZipFile(PREVIOUS) as old, ZipFile(UNSIGNED,"w",ZIP_DEFLATED) as z:
    for name in ("META-INF/com/google/android/update-binary","META-INF/com/android/otacert"):
        z.writestr(old.getinfo(name),old.read(name))
    for name,txt in (
        ("META-INF/com/google/android/updater-script","\n".join(edify)+"\n"),
        ("META-INF/g739-prepost.sh","\n".join(prepost)+"\n")):
        zi=ZipInfo(name);zi.compress_type=ZIP_DEFLATED;zi.external_attr=0o100755<<16
        z.writestr(zi,txt.encode())
    for n,s in FILES.items():z.write(s["source"],"payload/"+n)
subprocess.run([
   "java","-Xmx512m","-Djava.library.path="+str(A/"out/host/linux-x86/lib64"),
   "-jar",str(A/"out/host/linux-x86/framework/signapk.jar"),
   "-w",str(A/"build/target/product/security/testkey.x509.pem"),
   str(A/"build/target/product/security/testkey.pk8"),
   str(UNSIGNED),str(SIGNED)],check=True)
with ZipFile(SIGNED) as z:
    assert z.testzip() is None
    for n,s in FILES.items():
        assert sha(z.read("payload/"+n))==s["new"]
    names=set(z.namelist())
    assert "payload/boot.img" not in names
    assert "payload/system.img" not in names
    assert all(x not in "\n".join(edify).lower() for x in
       ("format(","delete_recursive(","wipe(", "mkfs.ntfs"))
manifest={
 "stage":"G739","device":"SM-T311","owner_attended_only":True,
 "boot_unchanged":True,"recovery_unchanged":True,
 "no_backups":True,"no_format":True,
 "new_policy_behavior":"exact wakeup subtrees labeled sysfs_wakeup despite changing wakeupN index",
 "git_device_sepolicy":"9273de6",
 "signed_sha256":sha(SIGNED.read_bytes()),
 "size_bytes":SIGNED.stat().st_size,
 "expected_physical_BOOT_sha256":BOOT_SHA,
 "files":{n:{"old_sha256":v["old"],"new_sha256":v["new"],
             "target":v["target"]} for n,v in FILES.items()}
}
(OUT/"G739_TWRP_MANIFEST.json").write_text(json.dumps(manifest,indent=2)+"\n")
print("G739_SIGNED_OWNER_TWRP_ZIP",SIGNED)
print("G739_SIGNATURE_SHA256",manifest["signed_sha256"],"SIZE",manifest["size_bytes"])
print("NOT INSTALLED BY THIS BUILDER")
