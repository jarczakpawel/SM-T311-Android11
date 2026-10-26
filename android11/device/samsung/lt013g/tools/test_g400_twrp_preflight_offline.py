#!/usr/bin/env python3
"""Test exact installed old TWRP ramdisk and G400 read-only install preflight.

No device access, no live partitions, no modem or recovery operations.
An extracted real TWRP CPIO from the physically hashed 8MiB image proves
whether shell, BusyBox and required applets are actually present.
Use synthetic local ordinary files for positive/fail-closed shell cases.
"""
import argparse
import hashlib
import lzma
from pathlib import Path
import re
import struct
import subprocess
import tempfile
from zipfile import ZipFile

TWRP_HASH='f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f'
G400_HASH='94cbd4e12acc308f86074cfb8e56e1099920d17353f17e35a5cb2759a7c19095'
NEWBOOT='cad5e76faf372beff3243ec615924286096713c994a6e5a03d3231f69478e6fd'
OLD_BOOT='f9292bab437bce12cea56e6e0c067a15c87475b69a61fc0c13fd4d3f07b9694f'
def require(condition,reason):
    if not condition: raise AssertionError(reason)

def recovery_files(image):
    img=image.read_bytes()
    require(hashlib.sha256(img).hexdigest()==TWRP_HASH,
            'wrong recovery image, not physically measured TWRP!')
    require(img[:8]==b'ANDROID!','wrong bootimg header')
    k=struct.unpack_from('<I',img,8)[0]
    r=struct.unpack_from('<I',img,16)[0]
    page=struct.unpack_from('<I',img,36)[0]
    offset=((page+k+page-1)//page)*page
    raw=lzma.decompress(img[offset:offset+r])
    files={};i=0
    while raw[i:i+6] in (b'070701',b'070702'):
        cells=[int(raw[i+6+8*j:i+14+8*j],16) for j in range(13)]
        mode=cells[1];size=cells[6];name_len=cells[11]
        name=raw[i+110:i+110+name_len].rstrip(b'\0').decode()
        i=(i+110+name_len+3)&~3
        content=raw[i:i+size];i=(i+size+3)&~3
        if name=='TRAILER!!!': break
        files[name]=(mode,content)
    return files

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--android',required=True,type=Path)
    p.add_argument('--project',required=True,type=Path)
    opt=p.parse_args()
    base=opt.android/'device/samsung/lt013g/tools'
    actual=(base/'g400_twrp_readonly_preflight.sh').read_text()
    ota=opt.project/'artifacts/SM-T311-G400-SUPL-ORPHAN-CLOSE-20261017/SM-T311-G400-GUARDED-GPS-SUPL-CP-CLOSE-SIGNED.zip'
    recovery=opt.project/'device/backups/v111-existing-recovery-20260906-135425/RECOVERY-current-8MiB.img'
    require(hashlib.file_digest(ota.open('rb'),'sha256').hexdigest()==G400_HASH,'wrong signed G400 ZIP')
    files=recovery_files(recovery)
    for path in ('sbin/sh','sbin/busybox','sbin/twrp','sbin/recovery',
                 'etc/twrp.fstab','etc/recovery.fstab','default.prop'):
        require(path in files,'real TWRP missing '+path)
    require(files['sbin/sh'][0]&0o170000==0o120000 and files['sbin/sh'][1]==b'busybox',
            'TWRP shell symlink is not BusyBox')
    require(files['sbin/busybox'][0]&0o111 != 0,'TWRP BusyBox not executable')
    binary=files['sbin/busybox'][1]
    for app in ('sha256sum','cut','dd','readlink','awk','id'):
        require(app.encode() in binary,'old TWRP BusyBox may lack needed '+app)
    fstab=files['etc/twrp.fstab'][1].decode()
    for path in ('/dev/block/platform/dw_mmc/by-name/BOOT',
                 '/dev/block/platform/dw_mmc/by-name/SYSTEM',
                 '/dev/block/platform/dw_mmc/by-name/OTA'):
        require(path in fstab,'installed TWRP fstab lacks '+path)
    with ZipFile(ota) as z:
        require(z.testzip() is None,'G400 ZIP CRC failure')
        guard=z.read('META-INF/t311-g379-check.sh').decode()
        updater=z.read('META-INF/com/google/android/updater-script').decode()
        boot=z.read('boot.img')
    require(hashlib.sha256(boot).hexdigest()==NEWBOOT,'G400 BOOT wrong')
    blocks=len(boot)//2048
    require(len(boot)==6838272 and blocks==3339,'G400 BOOT changed size')
    require('bs=2048 count='+str(blocks) in guard and
            'bs=2048 count='+str(blocks) in actual,'mismatched read-only BOOT SHA span')
    require(G400_HASH in actual and NEWBOOT in actual and OLD_BOOT in actual,'preflight not pinned to G400 guard')
    require(guard.count('/dev/block/platform/dw_mmc/by-name/BOOT')>=2 and
            guard.count('/dev/block/platform/dw_mmc/by-name/SYSTEM')>=2,
            'G400 guard TWRP aliases differ unexpectedly')
    for marker in ('assert(run_program("/sbin/sh", "/tmp/t311-g379-check.sh", "pre")',
                   'block_image_update(',
                   'package_extract_file("boot.img",',
                   'assert(run_program("/sbin/sh", "/tmp/t311-g379-check.sh", "post")'):
        require(marker in updater,'protected updater order missing '+marker)
    require(updater.index('"pre"')<updater.index('block_image_update(') <
            updater.index('package_extract_file("boot.img",')<updater.index('"post"'),
            'protected updater BOOT/SYSTEM write sequence changed')
    require(not re.search(r'(?m)^\s*(reboot|mount|umount|chcon|setprop|dd\s+[^\n]*\bof=|rm\s+|flash_image\s+)',actual),
            'read-only preflight accidentally contains mutation command')
    # The source uses dd only with if=; OTA's actual shell is not executed
    # against any real partition. Test only remapped synthetic regular files.
    require('dd if="$boot" bs=2048 count='+str(blocks) in actual,'read-only dd changed')
    check=subprocess.run(['sh','-n',str(base/'g400_twrp_readonly_preflight.sh')],
                         capture_output=True,text=True)
    require(check.returncode==0,'recovery preflight shell syntax failed: '+check.stderr)
    with tempfile.TemporaryDirectory(prefix='g390-preflight-synth-') as tmp:
        root=Path(tmp)
        (root/'boot').write_bytes(boot)
        (root/'system').write_bytes(b'synthetic-not-a-real-block-device')
        (root/'fake-twrp').write_text('simulated executable\n');(root/'fake-twrp').chmod(0o755)
        (root/'fake-sh').symlink_to('/bin/sh')
        (root/'signed-test').write_bytes(b'synthetic signed G400 fixture')
        (root/'mounts').write_text('')
        (root/'bootlink').symlink_to(root/'boot')
        (root/'systemlink').symlink_to(root/'system')
        script=actual
        changes={
         'bb=/sbin/busybox':'bb=/usr/bin/busybox',
         'signed=/data/local/tmp/SM-T311-G400-GUARDED-GPS-SUPL-CP-CLOSE-SIGNED.zip':'signed='+str(root/'signed-test'),
         'boot=/dev/block/mmcblk0p9':'boot='+str(root/'boot'),
         'system=/dev/block/mmcblk0p20':'system='+str(root/'system'),
         'bootlink=/dev/block/platform/dw_mmc/by-name/BOOT':'bootlink='+str(root/'bootlink'),
         'systemlink=/dev/block/platform/dw_mmc/by-name/SYSTEM':'systemlink='+str(root/'systemlink'),
         '[ -b "$boot" ]':'[ -f "$boot" ]',
         '[ -b "$system" ]':'[ -f "$system" ]',
         '[ -x /sbin/twrp ]':'[ -x '+str(root/'fake-twrp')+' ]',
         '[ -L /sbin/sh ]':'[ -L '+str(root/'fake-sh')+' ]',
         '$(id -u)':'$(printf 0)',
         '/proc/mounts':str(root/'mounts'),
         'want='+G400_HASH:'want='+hashlib.sha256((root/'signed-test').read_bytes()).hexdigest(),
        }
        for old,new in changes.items():
            require(old in script,'missing simulation remap '+old)
            script=script.replace(old,new)
        runner=root/'synthetic-test.sh'
        runner.write_text(script)
        def run():
            return subprocess.run(['sh',str(runner)],capture_output=True,text=True)
        good=run()
        require(good.returncode==0 and 'ALL READ-ONLY SAFETY GATES PASS' in good.stdout,
                'good synthetic TWRP preflight rejected: '+good.stderr)
        print('PASS source-extracted synthetic TWRP preflight with real signed BOOT prefix')
        mutants=[
           ('unknown_boot',lambda:(root/'boot').write_bytes(bytes(len(boot))),lambda:(root/'boot').write_bytes(boot)),
           ('corrupt_signed_ZIP',lambda:(root/'signed-test').write_bytes(b'corrupt'),lambda:(root/'signed-test').write_bytes(b'synthetic signed G400 fixture')),
           ('missing_BOOT_alias',lambda:(root/'bootlink').unlink(),lambda:(root/'bootlink').symlink_to(root/'boot')),
           ('wrong_SYSTEM_alias',lambda:((root/'systemlink').unlink(),(root/'systemlink').symlink_to(root/'boot')),lambda:((root/'systemlink').unlink(),(root/'systemlink').symlink_to(root/'system'))),
           ('SYSTEM_mounted',lambda:(root/'mounts').write_text('/dev/synthetic /system ext4 rw 0 0\n'),lambda:(root/'mounts').write_text('')),
           ('no_TWRP_binary',lambda:(root/'fake-twrp').chmod(0o644),lambda:(root/'fake-twrp').chmod(0o755)),
           ('no_shell_symlink',lambda:(root/'fake-sh').unlink(),lambda:(root/'fake-sh').symlink_to('/bin/sh')),
           ('no_signed_ZIP',lambda:(root/'signed-test').unlink(),lambda:(root/'signed-test').write_bytes(b'synthetic signed G400 fixture')),
        ]
        for name,disable,restore in mutants:
            disable()
            fail=run()
            require(fail.returncode!=0,'preflight FAIL OPEN on mutant: '+name)
            restore()
            require(run().returncode==0,'preflight not restored after: '+name)
            print('PASS fail-closed case',name)
    print('G400 READONLY TWRP PREFLIGHT TEST PASS: exact physically measured TWRP ramdisk+G400 signed ZIP guard, 8 negative synthetic cases, 0 real disk writes')
if __name__=='__main__':main()
