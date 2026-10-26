#!/usr/bin/env python3
"""Produce TWRP-compatible, BOOT-only G582 OTA using existing signed G587 binary.

Host generator ONLY.  No ADB, flashing, backup, or alterations to images.
"""
import argparse
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path
from zipfile import ZipFile, ZIP_STORED, ZIP_DEFLATED

ROOT=Path('/home/debian/android/lineage-18.1-sm-t311')
P=Path('/home/debian/Projekty/samsung')
G582=P/'linux-7.3-port/artifacts/G582-SAFE-KERNEL-XARRAY-PMU-DVFS-DIAG/BOOT-G582-LINUX73-PMU-XARRAY-DVFS-8MiB-HOST-ONLY.img'
G587=P/'artifacts/SM-T311-G586-VERIFIED-SYSTEM-20261020/G587-G586-TWRP-GUARDED-SIGNED.zip'
OLD='81b01abb5612a9a777330705c02c4bc1f49386da4d5df0950f955246f9fa6bce'
NEW='f7c5091dcba5cbbf012d9eab5ac4a505834a813e272442d8ad8c00c24ae6ee65'
SYSTEM='2b7f9571b14f3a85e325fd0ef7f9cf1c9fdd56f401444210dbc8ae2dd1a6bd09'
TWRP='f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f'
UPDATER='META-INF/com/google/android/updater-script'
GUARD='META-INF/t311-g589-boot-guard.sh'
def sha(data):return hashlib.sha256(data).hexdigest()
def filehash(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def guard():
    return '''#!/sbin/sh
set -eu
bb=/sbin/busybox
boot=/dev/block/mmcblk0p9
recovery=/dev/block/mmcblk0p10
system=/dev/block/mmcblk0p20
fail() { echo "G589 BOOT UPDATE REFUSED: $1" >&2; exit 1; }
hash() { "$bb" sha256sum "$1" | "$bb" cut -d" " -f1; }
[ -b "$boot" ] && [ -b "$system" ] && [ -b "$recovery" ] || fail "missing expected block"
[ "$("$bb" blockdev --getsize64 "$boot")" = "8388608" ] || fail "BOOT size changed"
[ "$("$bb" blockdev --getsize64 "$system")" = "2235564032" ] || fail "SYSTEM size changed"
[ -L /dev/block/platform/dw_mmc/by-name/BOOT ] || fail "missing TWRP BOOT alias"
[ "$("$bb" readlink -f /dev/block/platform/dw_mmc/by-name/BOOT)" = "$boot" ] || fail "BOOT alias mismatch"
[ "$(hash "$recovery")" = "''' + TWRP + '''" ] || fail "RECOVERY unexpected"
case "$1" in
  pre)
    [ "$(hash "$boot")" = "''' + OLD + '''" ] || fail "unknown installed BOOT"
    [ "$(hash "$system")" = "''' + SYSTEM + '''" ] || fail "installed SYSTEM not G586"
    ;;
  post)
    [ "$(hash "$boot")" = "''' + NEW + '''" ] || fail "new G582 BOOT hash mismatch"
    ;;
  *) fail "invalid mode" ;;
esac
exit 0
'''
def updater():
    return '''assert(getprop("ro.product.device") == "lt013g" ||
    getprop("ro.product.device") == "lt013gxx" ||
    file_getprop("/default.prop", "ro.product.device") == "lt013g" ||
    abort("G589 blocked: not SM-T311 lt013g"));
ui_print("G589: G582 Linux 7.3 Exynos PMU XArray diagnostic BOOT-only");
assert(package_extract_file("'''+GUARD+'''", "/tmp/t311-g589-boot-guard.sh"));
assert(run_program("/sbin/sh", "/tmp/t311-g589-boot-guard.sh", "pre") == "0");
ui_print("G589 G586 SYSTEM and old BOOT preflight PASS");
assert(package_extract_file("boot.img", "/dev/block/platform/dw_mmc/by-name/BOOT"));
assert(run_program("/sbin/sh", "/tmp/t311-g589-boot-guard.sh", "post") == "0");
ui_print("G589 BOOT postflash hash PASS");
set_progress(1.000000);
'''
def build(out):
    if out.exists():raise ValueError('refusing overwrite output')
    assert G582.stat().st_size==8388608 and filehash(G582)==NEW
    assert filehash(G587)=='157e4fcce298e3fab7f54b9cf6087cb31060022e4f247a1e2b5b92201a4bac2a'
    with ZipFile(G587) as z:
        updater_binary=z.read('META-INF/com/google/android/update-binary')
    with tempfile.TemporaryDirectory(prefix='g589-boot-only-') as tmp:
        unsigned=Path(tmp)/'g589-boot-unsigned.zip'
        with ZipFile(unsigned,'w',ZIP_DEFLATED) as z:
            z.writestr(UPDATER,updater())
            z.writestr(GUARD,guard())
            z.writestr('META-INF/com/google/android/update-binary',updater_binary,compress_type=ZIP_STORED)
            z.write(G582,'boot.img',compress_type=ZIP_STORED)
        cmd=['java','-Xmx900m','-Djava.library.path='+str(ROOT/'out/host/linux-x86/lib64'),
             '-jar',str(ROOT/'out/host/linux-x86/framework/signapk.jar'),'-w',
             str(ROOT/'build/make/target/product/security/testkey.x509.pem'),
             str(ROOT/'build/make/target/product/security/testkey.pk8'),
             str(unsigned),str(out)]
        subprocess.run(cmd,check=True,cwd=ROOT)
    with ZipFile(out) as z:
        assert z.testzip() is None
        assert z.read('boot.img')==G582.read_bytes()
        assert z.read(UPDATER).decode()==updater()
        assert z.read(GUARD).decode()==guard()
        assert set(z.namelist()).issuperset({UPDATER,GUARD,'boot.img','META-INF/com/google/android/update-binary'})
        assert not any(f in z.namelist() for f in ('system.new.dat.br','system.transfer.list','recovery.img','userdata.img','radio.img'))
    result={'release':'G589','status':'SIGNED_HOST_BOOT_ONLY_NOT_FLASHED','image_sha256':NEW,
            'old_boot_sha256':OLD,'required_system_sha256':SYSTEM,'recovery_sha256':TWRP,
            'signed_sha256':filehash(out),'signed_size':out.stat().st_size,'output':str(out)}
    print(json.dumps(result,indent=2))
    return result
if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--report',type=Path)
    args=parser.parse_args()
    res=build(args.output)
    if args.report:args.report.write_text(json.dumps(res,indent=2)+'\n')
