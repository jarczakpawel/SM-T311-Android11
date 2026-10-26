#!/usr/bin/env python3
"""G592 G591 signed SYSTEM-only OTA for SM-T311's original TWRP3.1.1.

Converts the signed AOSP non-A/B full OTA to an exact verified SYSTEM-only
payload. Deliberately drops *boot.img* rather than downgrading kernel G582
with the older kernel G551 from the AOSP target files.

HOST ONLY; does not communicate with, alter or back up the tablet.
"""
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED
import argparse
import hashlib
import json
import subprocess
import tempfile

ROOT=Path('/home/debian/android/lineage-18.1-sm-t311')
G591_ZIP_SHA='b502076d0dfa77fbbe20cb056c27bd613c181660239fbe20e5d74e6c15c4ea4c'
G591_NEW_SYSTEM='229e1a8cbf037fe68cfa8d190771f13c0207b46c0c31f874d0af656ebbf31ab1'
G586_OLD_SYSTEM='2b7f9571b14f3a85e325fd0ef7f9cf1c9fdd56f401444210dbc8ae2dd1a6bd09'
G582_BOOT='f7c5091dcba5cbbf012d9eab5ac4a505834a813e272442d8ad8c00c24ae6ee65'
G551_OTA_BOOT='81b01abb5612a9a777330705c02c4bc1f49386da4d5df0950f955246f9fa6bce'
ORIGINAL_TWRP='f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f'
UPDATER='META-INF/com/google/android/updater-script'
GUARD='META-INF/t311-g592-system-only-prepost.sh'
def sha_file(path):
    with path.open('rb') as f: return hashlib.file_digest(f,'sha256').hexdigest()
def sha_bytes(data):return hashlib.sha256(data).hexdigest()

def guard():
    return f'''#!/sbin/sh
set -eu
bb=/sbin/busybox
system=/dev/block/mmcblk0p20
boot=/dev/block/mmcblk0p9
recovery=/dev/block/mmcblk0p10
fail() {{ echo "G592 SYSTEM-ONLY REFUSED: $1" >&2; exit 1; }}
hash() {{ "$bb" sha256sum "$1" | "$bb" cut -d" " -f1; }}
[ -b "$system" ] && [ -b "$boot" ] && [ -b "$recovery" ] || fail "missing device block"
[ "$("$bb" blockdev --getsize64 "$system")" = "2235564032" ] || fail "SYSTEM partition geometry changed"
[ "$("$bb" blockdev --getsize64 "$boot")" = "8388608" ] || fail "BOOT geometry changed"
[ -L /dev/block/platform/dw_mmc/by-name/SYSTEM ] || fail "TWRP SYSTEM alias missing"
[ "$("$bb" readlink -f /dev/block/platform/dw_mmc/by-name/SYSTEM)" = "$system" ] || fail "SYSTEM TWRP alias mismatch"
if "$bb" grep -q ' /system ' /proc/mounts; then fail "SYSTEM mounted"; fi
[ "$(hash "$boot")" = "{G582_BOOT}" ] || fail "G582 BOOT must stay installed"
[ "$(hash "$recovery")" = "{ORIGINAL_TWRP}" ] || fail "original recovery required"
case "$1" in
 pre)
    [ "$(hash "$system")" = "{G586_OLD_SYSTEM}" ] || fail "not physical G586 SYSTEM"
    ;;
 post)
    [ "$(hash "$system")" = "{G591_NEW_SYSTEM}" ] || fail "new G591 SYSTEM SHA mismatch"
    ;;
 *) fail "bad phase" ;;
esac
exit 0
'''
def rewrite(original:str):
    old_path='/dev/block/platform/soc/12550000.mmc/by-name/SYSTEM'
    new_path='/dev/block/platform/dw_mmc/by-name/SYSTEM'
    old_boot='package_extract_file("boot.img", "/dev/block/platform/soc/12550000.mmc/by-name/BOOT");'
    marker='ui_print("Patching system image unconditionally...");'
    if original.count(old_path)!=1 or original.count(old_boot)!=1 or original.count(marker)!=1:
        raise ValueError('Unknown source OTA updater partition layout')
    if original.count('block_image_update(')!=1:
        raise ValueError('G591 OTA must have exactly one SYSTEM block update')
    if any(x in original for x in ('format(', 'wipe_data', 'recovery.img', '"/data"')):
        raise ValueError('Unexpected destructive OTA operation')
    pre=(f'assert(package_extract_file("{GUARD}", "/tmp/t311-g592-system-only-prepost.sh"));\n'
         'assert(run_program("/sbin/sh", "/tmp/t311-g592-system-only-prepost.sh", "pre") == "0");\n')
    post='assert(run_program("/sbin/sh", "/tmp/t311-g592-system-only-prepost.sh", "post") == "0");\n'
    result=original.replace(old_path,new_path).replace(marker,pre+marker).replace(old_boot,post)
    if 'boot.img' in result or '/by-name/BOOT' in result:
        raise ValueError('G592 would overwrite BOOT')
    if (result.count('block_image_update(')!=1 or result.count(GUARD)!=1 or
            result.count('run_program("/sbin/sh", "/tmp/t311-g592-system-only-prepost.sh"')!=2):
        raise ValueError('G592 unverified guard insertion')
    if not(result.index('"pre"')<result.index('block_image_update(')<result.index('"post"')):
        raise ValueError('G592 wrong guard order')
    return result

def build(source:Path,out:Path):
    if out.exists() or source.resolve()==out.resolve():
        raise ValueError('Refuse existing output or input overwrite')
    if sha_file(source)!=G591_ZIP_SHA:
        raise ValueError('Wrong G591 official AOSP OTA SHA')
    from g408_generate_pinned_twrp_preflight import validate_full_system_transfer
    with ZipFile(source) as z:
        if z.testzip() is not None:raise ValueError('Source OTA CRC')
        names=set(z.namelist())
        if not set(('boot.img','system.transfer.list','system.new.dat.br','system.patch.dat',UPDATER,'META-INF/com/google/android/update-binary')).issubset(names):
            raise ValueError('Source OTA required members missing')
        if len(z.read('boot.img'))!=8388608 or sha_bytes(z.read('boot.img'))!=G551_OTA_BOOT:
            raise ValueError('AOSP BOOT unexpected -- never install')
        validate_full_system_transfer(z.read('system.transfer.list'))
        script=rewrite(z.read(UPDATER).decode())
        with tempfile.TemporaryDirectory(prefix='g592-g591-systemonly-') as temp:
            unsigned=Path(temp)/'unsigned-system-only.zip'
            with ZipFile(unsigned,'w',ZIP_DEFLATED,allowZip64=True) as work:
                for info in z.infolist():
                    if info.filename=='boot.img':continue
                    body=script.encode() if info.filename==UPDATER else z.read(info.filename)
                    work.writestr(info,body,compress_type=info.compress_type)
                work.writestr(GUARD,guard(),compress_type=ZIP_DEFLATED)
            subprocess.run(['java','-Xmx1400m',
              '-Djava.library.path='+str(ROOT/'out/host/linux-x86/lib64'),
              '-jar',str(ROOT/'out/host/linux-x86/framework/signapk.jar'),'-w',
              str(ROOT/'build/make/target/product/security/testkey.x509.pem'),
              str(ROOT/'build/make/target/product/security/testkey.pk8'),
              str(unsigned),str(out)],check=True,cwd=ROOT)
    with ZipFile(out) as z:
        assert z.testzip() is None
        assert 'boot.img' not in z.namelist() and 'recovery.img' not in z.namelist()
        assert z.read(UPDATER).decode()==script
        assert z.read(GUARD).decode()==guard()
        with ZipFile(source) as prior:
            for name in ('system.new.dat.br','system.patch.dat','system.transfer.list',
                         'META-INF/com/google/android/update-binary'):
                assert sha_bytes(z.read(name))==sha_bytes(prior.read(name))
    result={'release':'G592-G591','status':'SIGNED_TWRP_SYSTEM_ONLY_HOST_VERIFIED_NOT_FLASHED',
            'original_aosp_sha256':G591_ZIP_SHA,'target_system_sha256':G591_NEW_SYSTEM,
            'installed_system_required_sha256':G586_OLD_SYSTEM,
            'boot_g582_kept_sha256':G582_BOOT,'recovery_sha256':ORIGINAL_TWRP,
            'signed_sha256':sha_file(out),'signed_bytes':out.stat().st_size,
            'flash_performed':False}
    print(json.dumps(result,indent=2))
    return result
if __name__=='__main__':
    p=argparse.ArgumentParser()
    p.add_argument('--source',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--report',type=Path)
    args=p.parse_args()
    output=build(args.source,args.output)
    if args.report:args.report.write_text(json.dumps(output,indent=2)+'\n')
