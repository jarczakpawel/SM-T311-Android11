#!/usr/bin/env python3
"""G735 supervised, strict-SHA TWRP incremental for SM-T311, no backups.

Build a standard test-key signed legacy recovery ZIP using known-working
Android 11 update-binary. NO full SYSTEM format, NO modem/GNSS/RIL changes.
Only enter recovery with owner physically present; never auto-flash.
"""
from pathlib import Path
from zipfile import ZipFile,ZipInfo,ZIP_DEFLATED
from hashlib import sha256
import subprocess,json

A=Path(__file__).resolve().parents[4]
P=Path("/home/debian/Projekty/samsung/linux-7.3-port")
BASE=P/"artifacts/G735-OTG-TWRP-OWNER-ATTENDED"
BASE.mkdir(parents=True,exist_ok=True)
OLDZIP=P.parent/"artifacts/SM-T311-G354-EMPTY-PIN-PUK-GUARD-20261016/SM-T311-G354-SIM-RETRY-GUARD-TWRP-SIGNED.zip"
UNSIGNED=BASE/"SM-T311-G735-OTG-OWNER-TWRP-UNSIGNED.zip"
SIGNED=BASE/"SM-T311-G735-OTG-OWNER-TWRP-SIGNED.zip"
BOOT=P/"artifacts/G730-OTG-ARM32-TEST-ONLY/BOOT-G730-OTG-EXPERIMENTAL-8MiB-DO-NOT-FLASH.img"
BUILD=A/"out/target/product/lt013g/system"
payload={
 "boot.img":(BOOT,"/dev/block/mmcblk0p9","dcc7aafe81bb947e9afb4c8d1c4d49bbbeb413f65164c014cb02d392ae57f9bc","e53ad50d43e8aa92beccf537b5d2c9cc955f91d8fe2ff64d76a7f174b311177b",None),
 "gadget":(BUILD/"vendor/bin/hw/android.hardware.usb.gadget@1.1-service","/system/system/vendor/bin/hw/android.hardware.usb.gadget@1.1-service","726f705d8d624733746f9aeb3fe2020fa481ea7db67158072906da2634d38647","fe0cc1bb9ba1caed0603f4cc8175d593af20b1cd585ea69ffaaffb785d9fa075","u:object_r:hal_usb_gadget_default_exec:s0"),
 "vendor_cil":(BUILD/"vendor/etc/selinux/vendor_sepolicy.cil","/system/system/vendor/etc/selinux/vendor_sepolicy.cil","4647245a44106bf0e4161de9bab7c52e1bb2799ecf5273c0cff27e13975462d1","a3b5dc65cbe28bca151fbfa3a0998b6c4d2262bceb65cf33c5881b8a39fe5dfe","u:object_r:vendor_configs_file:s0"),
 "vendor_precompiled":(BUILD/"vendor/etc/selinux/precompiled_sepolicy","/system/system/vendor/etc/selinux/precompiled_sepolicy","ec5cc7e49014ce8954c423f0cdf76121876248f5b61680a2a6eaedd45c45a8e9","8263d8529e064a725454d4d0fee57f03c75b951fa576d770f7704b804e1515eb","u:object_r:vendor_configs_file:s0"),
 "root_sepolicy":(A/"out/target/product/lt013g/root/sepolicy","/system/sepolicy","ec5cc7e49014ce8954c423f0cdf76121876248f5b61680a2a6eaedd45c45a8e9","8263d8529e064a725454d4d0fee57f03c75b951fa576d770f7704b804e1515eb","u:object_r:sepolicy_file:s0"),
}
def sha(p):
    h=sha256()
    with p.open("rb") as f:
        for piece in iter(lambda:f.read(1048576),b""): h.update(piece)
    return h.hexdigest()

for name,(src,path,old,new,label) in payload.items():
    assert src.is_file() and sha(src)==new,(name,sha(src),new)
assert BOOT.stat().st_size==8388608

edify=[
 'ui_print("SM-T311 G735: G730 OTG kernel + G733 USB Gadget HAL + G731 SELinux");',
 'ui_print("SUPER-VISED update, strictly SHA-guarded, no data wipe or backup");',
 'assert(getprop("ro.product.device") == "lt013g" || getprop("ro.build.product") == "lt013g" || getprop("ro.product.device") == "lt013gxx" || getprop("ro.build.product") == "lt013gxx" || getprop("ro.product.device") == "SM-T311" || file_getprop("/default.prop","ro.product.device") == "lt013g");',
 'ifelse(is_mounted("/system"),unmount("/system"),ui_print("Mount SYSTEM"));',
 'assert(mount("ext4","EMMC","/dev/block/mmcblk0p20","/system","rw"));',
 'assert(file_getprop("/system/system/build.prop","ro.build.version.sdk") == "30");',
 'assert(package_extract_file("META-INF/g735-prepost.sh","/tmp/g735-prepost.sh"));',
 'assert(run_program("/sbin/sh","/tmp/g735-prepost.sh","pre") == "0");',
]
for name,(src,target,old,new,label) in payload.items():
    edify.append(f'assert(package_extract_file("payload/{name}","/tmp/g735-{name}"));')
edify.append('assert(run_program("/sbin/sh","/tmp/g735-prepost.sh","stage") == "0");')
for name,(src,target,old,new,label) in payload.items():
    if name=="boot.img":continue
    tmp=target+".g735-staging"
    edify.append(f'assert(package_extract_file("payload/{name}","{tmp}"));')
    mode="0755" if name=="gadget" else "0644"
    gid="2000" if name=="gadget" else "0"
    edify.append(f'set_metadata("{tmp}","uid",0,"gid",{gid},"mode",{mode},"selabel","{label}");')
edify.append('assert(run_program("/sbin/sh","/tmp/g735-prepost.sh","activate") == "0");')
edify.append('assert(run_program("/sbin/sh","/tmp/g735-prepost.sh","after-system") == "0");')
edify.append('ui_print("Installing SHA-verified G730 BOOT image LAST");')
edify.append('assert(package_extract_file("payload/boot.img","/dev/block/mmcblk0p9"));')
edify.append('assert(run_program("/sbin/sh","/tmp/g735-prepost.sh","post") == "0");')
edify.append('unmount("/system");')
edify.append('ui_print("G735 installed; reboot Android and inspect modem/ADB/OTG");')

checks=["#!/sbin/sh","set -eu","bb=/sbin/busybox",
        'hash() { "$bb" sha256sum "$1" | "$bb" cut -d" " -f1; }',
        'die() { echo "G735 $1 FAILED" >&2; exit "$2"; }',
        '[ "$(hash /dev/block/mmcblk0p10)" = "f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f" ] || die RECOVERY 60',
        '[ -d /system/system/vendor ] || die SYSTEM_LAYOUT 61',
        '[ -e /system/system/vendor/lib/hw/gps.default.so ] || die GPS_PRESENCE 62',
        'case "$1" in',
        "pre)",
]
for i,(name,(src,target,old,new,label)) in enumerate(payload.items()):
    checks.append(f' [ "$(hash "{target}")" = "{old}" ] || die PRE_{name.replace(".","_")} {70+i}')
checks += [" ;;","stage)"]
for i,(name,(src,target,old,new,label)) in enumerate(payload.items()):
    checks.append(f' [ "$(hash "/tmp/g735-{name}")" = "{new}" ] || die STAGE_{name.replace(".","_")} {80+i}')
checks += [" ;;","activate)"]
for i,(name,(src,target,old,new,label)) in enumerate(payload.items()):
    if name=="boot.img":continue
    checks.append(f' [ "$(hash "{target}.g735-staging")" = "{new}" ] || die STAGED_{name.replace(".","_")} {90+i}')
for i,(name,(src,target,old,new,label)) in enumerate(payload.items()):
    if name=="boot.img":continue
    checks.append(f' "$bb" mv -f "{target}.g735-staging" "{target}" || die ACTIVATE_{name.replace(".","_")} {100+i}')
checks += [' "$bb" sync'," ;;","after-system)"]
for i,(name,(src,target,old,new,label)) in enumerate(payload.items()):
    if name=="boot.img":continue
    checks.append(f' [ "$(hash "{target}")" = "{new}" ] || die POST_SYSTEM_{name.replace(".","_")} {110+i}')
checks += [' "$bb" sync'," ;;","post)"]
for i,(name,(src,target,old,new,label)) in enumerate(payload.items()):
    checks.append(f' [ "$(hash "{target}")" = "{new}" ] || die POST_{name.replace(".","_")} {120+i}')
checks += [' "$bb" sync',' ;;','*) exit 99;;','esac']
with ZipFile(OLDZIP) as old,ZipFile(UNSIGNED,"w",ZIP_DEFLATED) as z:
    for p in ("META-INF/com/google/android/update-binary","META-INF/com/android/otacert"):
        z.writestr(old.getinfo(p),old.read(p))
    for name,data in [("META-INF/com/google/android/updater-script","\n".join(edify)+"\n"),("META-INF/g735-prepost.sh","\n".join(checks)+"\n")]:
        info=ZipInfo(name);info.compress_type=ZIP_DEFLATED;info.external_attr=0o100755<<16
        z.writestr(info,data.encode())
    for name,(src,_,_,_,_) in payload.items():
        z.write(src,"payload/"+name)
subprocess.run(["java","-Xmx512m","-Djava.library.path="+str(A/"out/host/linux-x86/lib64"),"-jar",str(A/"out/host/linux-x86/framework/signapk.jar"),"-w",str(A/"build/target/product/security/testkey.x509.pem"),str(A/"build/target/product/security/testkey.pk8"),str(UNSIGNED),str(SIGNED)],check=True)
with ZipFile(SIGNED) as z:
    assert z.testzip() is None
    for name,(src,path,old,new,label) in payload.items():
        assert sha256(z.read("payload/"+name)).hexdigest()==new
    assert "system.new.dat" not in z.namelist()
manifest={
    "stage":"G735","device":"SM-T311",
    "requires_owner_present":True,"status":"SIGNED_TEST_ARTIFACT_NOT_INSTALLED",
    "type":"TWRP SHA_GUARDED_FILE_DELTA_WITH_LAST_BOOT",
    "no_backup":True,"no_userdata_format":True,
    "signed_zip_sha256":sha(SIGNED),"signed_zip_bytes":SIGNED.stat().st_size,
    "preflight_recovery_sha":"f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f",
    "files":{n:{"target":dest,"old":old,"new":new} for n,(_,dest,old,new,_) in payload.items()}
}
(BASE/"G735_OWNER_TWRP_MANIFEST.json").write_text(json.dumps(manifest,indent=2)+"\n")
print("G735_OWNER_TWRP_SIGNED_BUILT",SIGNED)
print("G735_SHA",sha(SIGNED),"SIZE",SIGNED.stat().st_size)
print("No tablet action performed")
