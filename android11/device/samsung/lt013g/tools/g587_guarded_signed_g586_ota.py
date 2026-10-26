#!/usr/bin/env python3
"""Re-sign an existing AOSP non-A/B OTA with strict SM-T311 BOOT pre/post checks.

This is only a host package authoring tool, not a flashing script. The source
OTA must have block_image_update(SYSTEM), then package_extract_file(BOOT),
without recovery/data writes. The device guard verifies the exact previous
physical G373 BOOT prefix and exact by-name partition aliases in TWRP before
writing SYSTEM; after updating it verifies the new G378 BOOT contents.

The AOSP full-system updater still owns the SYSTEM block transaction.
No modem, SIM, EFS or recovery operations are included.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile
from zipfile import ZipFile,ZIP_DEFLATED,ZIP_STORED

ROOT=Path("/home/debian/android/lineage-18.1-sm-t311")
OLD_BOOT_SHA="81b01abb5612a9a777330705c02c4bc1f49386da4d5df0950f955246f9fa6bce"
NEW_BOOT_SHA="81b01abb5612a9a777330705c02c4bc1f49386da4d5df0950f955246f9fa6bce"
SCRIPT="META-INF/t311-g587-check.sh"
UPDATER="META-INF/com/google/android/updater-script"

def sha(b:bytes)->str:return hashlib.sha256(b).hexdigest()
def guard(boot_bytes:int)->str:
    if boot_bytes<=0 or boot_bytes%2048 or boot_bytes>8388608:
        raise ValueError("Invalid BOOT image size")
    blocks=boot_bytes//2048
    return """#!/sbin/sh
set -eu
bb=/sbin/busybox
boot=/dev/block/mmcblk0p9
system=/dev/block/mmcblk0p20
fail() { echo "G587 SAFE OTA BLOCK: $1" >&2; exit 1; }
hash() { "$bb" sha256sum "$1" | "$bb" cut -d" " -f1; }
boothead() { "$bb" dd if="$boot" bs=2048 count=NN 2>/dev/null | "$bb" sha256sum | "$bb" cut -d" " -f1; }
[ -b "$boot" ] || fail "no BOOT block"
[ -b "$system" ] || fail "no SYSTEM block"
[ -L /dev/block/platform/dw_mmc/by-name/BOOT ] || fail "BOOT alias absent"
[ -L /dev/block/platform/dw_mmc/by-name/SYSTEM ] || fail "SYSTEM alias absent"
[ "$("$bb" readlink -f /dev/block/platform/dw_mmc/by-name/BOOT)" = "$boot" ] || fail "BOOT alias mismatch"
[ "$("$bb" readlink -f /dev/block/platform/dw_mmc/by-name/SYSTEM)" = "$system" ] || fail "SYSTEM alias mismatch"
[ "$("$bb" blockdev --getsize64 "$system")" = "2235564032" ] || fail "SYSTEM physical geometry mismatch"
if "$bb" grep -q ' /system ' /proc/mounts; then fail "SYSTEM already mounted"; fi
case "$1" in
 pre)
   current="$(boothead)"
   if [ "$current" != "OLD" ] && [ "$current" != "NEW" ]; then
     fail "unknown physical BOOT prefix; refuse SYSTEM update"
   fi
   ;;
 post)
   [ "$(boothead)" = "NEW" ] || fail "updated BOOT SHA mismatch"
   ;;
 *) fail "invalid execution phase";;
esac
exit 0
""".replace('NN',str(blocks)).replace('OLD',OLD_BOOT_SHA).replace('NEW',NEW_BOOT_SHA)

def build(inp:Path,out:Path)->dict:
    if inp.resolve()==out.resolve():raise ValueError("Never overwrite source signed OTA")
    if out.exists():raise ValueError("Refuse overwriting an existing signed OTA")
    source_sha = sha(inp.read_bytes())
    if source_sha != "6591a84b7f522b9471ba209ab75a958c9affeb4861ddc938d1f7d1d96015a69a":
        raise ValueError("G587 exact original AOSP G586 OTA SHA mismatch")
    with ZipFile(inp) as original:
        if original.testzip() is not None:raise ValueError("Original ZIP CRC failure")
        names=set(original.namelist())
        if SCRIPT in names:raise ValueError("Guard already present")
        boot=original.read("boot.img")
        if len(boot) != 8388608 or sha(boot)!=NEW_BOOT_SHA:
            raise ValueError("G587 BOOT must match verified current p9 8MiB")
        if "recovery.img" in names or "userdata.img" in names or "radio.img" in names:
            raise ValueError("G587 unwanted partition payload")
        from g408_generate_pinned_twrp_preflight import validate_full_system_transfer
        validate_full_system_transfer(original.read("system.transfer.list"))
        code=original.read(UPDATER).decode()
        source_path='/dev/block/platform/soc/12550000.mmc/by-name/'
        # Both aliases physically verified on the REAL 2017 TWRP 3.1.1.
        if code.count(source_path+'SYSTEM')!=1 or code.count(source_path+'BOOT')!=1:
            raise ValueError("G587 unknown original updater block aliases")
        code=code.replace(source_path+'SYSTEM',
             '/dev/block/platform/dw_mmc/by-name/SYSTEM')
        code=code.replace(source_path+'BOOT',
             '/dev/block/platform/dw_mmc/by-name/BOOT')
        sys_marker='ui_print("Patching system image unconditionally...");'
        end_marker='package_extract_file("boot.img", "/dev/block/platform/dw_mmc/by-name/BOOT");'
        if code.count(sys_marker)!=1 or code.count(end_marker)!=1 or code.count("block_image_update(")!=1:
            raise ValueError("Unknown updater structure")
        for forbidden in ("format(", "wipe_data", '"/data"', "recovery.img", "/dev/block/mmcblk0p9", "/dev/block/mmcblk0p10", "block_image_update(\"/data"):
            if forbidden in code:raise ValueError("Forbidden OTA operation: "+forbidden)
        pre=('assert(package_extract_file("'+SCRIPT+'", "/tmp/t311-g587-check.sh"));\n'
             'assert(run_program("/sbin/sh", "/tmp/t311-g587-check.sh", "pre") == "0");\n')
        post='assert(run_program("/sbin/sh", "/tmp/t311-g587-check.sh", "post") == "0");\n'
        new=code.replace(sys_marker,pre+sys_marker)
        new=new.replace(end_marker,end_marker+'\n'+post)
        with tempfile.TemporaryDirectory(prefix="g587-package-") as temp:
            unsigned=Path(temp)/"guarded-unsigned.zip"
            with ZipFile(unsigned,"w",ZIP_DEFLATED,allowZip64=True) as work:
                for entry in original.infolist():
                    data=new.encode() if entry.filename==UPDATER else original.read(entry.filename)
                    work.writestr(entry,data,compress_type=entry.compress_type)
                work.writestr(SCRIPT,guard(len(boot)).encode(),compress_type=ZIP_DEFLATED)
            subprocess.run([
                "java","-Xmx1400m",
                "-Djava.library.path="+str(ROOT/"out/host/linux-x86/lib64"),
                "-jar",str(ROOT/"out/host/linux-x86/framework/signapk.jar"),"-w",
                str(ROOT/"build/make/target/product/security/testkey.x509.pem"),
                str(ROOT/"build/make/target/product/security/testkey.pk8"),
                str(unsigned),str(out)],check=True,cwd=ROOT)
    with ZipFile(out) as final:
        if final.testzip() is not None:raise ValueError("Signed guarded ZIP CRC failure")
        stored=final.read(UPDATER).decode()
        if stored!=new or final.read(SCRIPT).decode()!=guard(len(boot)):
            raise ValueError("Signed archive content mismatch")
        if sha(final.read("boot.img"))!=NEW_BOOT_SHA:raise ValueError("BOOT mutated")
        if stored.count('run_program("/sbin/sh", "/tmp/t311-g587-check.sh"')!=2:
            raise ValueError("Missing guarded stages")
        if not(stored.index('"pre"')<stored.index("block_image_update(")<stored.index('"post"')):
            raise ValueError("Unsafe execution order")
        for file in ("system.new.dat.br","system.transfer.list","system.patch.dat",
                     "META-INF/com/google/android/update-binary"):
            with ZipFile(inp) as old:
                if sha(final.read(file))!=sha(old.read(file)):raise ValueError("SYSTEM/installer payload changed")
    return {"status":"SIGNED_OTA_GUARDED_OFFLINE_PASS","path":str(out),
            "sha256":sha(out.read_bytes()),"bytes":out.stat().st_size,
            "old_boot_sha256":OLD_BOOT_SHA,"new_boot_sha256":NEW_BOOT_SHA,
            "guarded_pre_system":True,"guarded_post_boot":True,
            "device_flash_performed":False,"recovery_untouched":True,
            "data_wipe":False}

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--in",dest="original",required=True,type=Path)
    p.add_argument("--out",required=True,type=Path)
    p.add_argument("--report",type=Path)
    args=p.parse_args()
    result=build(args.original,args.out)
    if args.report:args.report.write_text(json.dumps(result,indent=2)+"\n")
    print(result["status"],result["sha256"],result["bytes"])
if __name__=="__main__":main()
