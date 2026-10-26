#!/usr/bin/env python3
"""G388 fail-closed cumulative full-OTA target-files stage.

G387's actual corrected XMM6262 PDP gateway and intact bootclasspath remain,
while G388 adds stock SM-T311 OTA(p11) /misc to the *normal boot* vendor
fstab selected by AOSP fs_mgr. Does NOT touch BOOT/RECOVERY/RIL/CP/EFS
or execute a reboot/mount/flash/wipe.
"""
import argparse
import hashlib
from io import BytesIO
import json
from pathlib import Path
import subprocess
import sys
from zipfile import ZipFile

p=argparse.ArgumentParser()
p.add_argument('source',type=Path)
p.add_argument('output',type=Path)
p.add_argument('--baseline-g387',type=Path,required=True)
p.add_argument('--baseline-g382',type=Path,required=True)
p.add_argument('--android',type=Path,required=True)
a=p.parse_args()
folder=Path(__file__).parent
subprocess.run([sys.executable,str(folder/'test_g388_recovery_bcb_fstab.py'),
                '--android',str(a.android)],check=True)
oldfile='SYSTEM/vendor/etc/fstab.lt013g'
now=(a.android/'device/samsung/lt01-common/rootdir/fstab.lt013g.vendor').read_bytes()
assert a.source.resolve()!=a.output.resolve() and not a.output.exists()
with ZipFile(a.baseline_g387) as old, ZipFile(a.source) as new:
    existing=old.read(oldfile)
    changed=new.read(oldfile)
    assert b'/misc' not in existing,'G387 original expected missing /misc'
    assert b'/misc emmc defaults defaults' in changed,'G388 /misc entry absent'
    assert changed==now,'OTA fstab not byte-identical to new device source'
    assert changed.startswith(existing.rstrip(b'\n')+b'\n'),'unexpected rewrite of old vendor mounts'
    assert new.read('BOOT/RAMDISK/fstab.lt013g')==old.read('BOOT/RAMDISK/fstab.lt013g'), 'ramdisk changed'
    immutable=[
       'IMAGES/boot.img','IMAGES/recovery.img',
       'SYSTEM/vendor/lib/libril.so','SYSTEM/lib/libaudiohal.so',
       'SYSTEM/vendor/etc/selinux/vendor_sepolicy.cil',
       'SYSTEM/framework/telephony-common.jar',
       'SYSTEM/framework/arm/boot-telephony-common.oat',
       'SYSTEM/framework/boot-telephony-common.vdex',
       'SYSTEM/framework/arm/boot-telephony-common.art',
    ]
    for name in immutable:
        if new.read(name)!=old.read(name):
            raise SystemExit('G388 unexpected mutation of previous immutable boot/telephony/GPS component: '+name)
    with ZipFile(BytesIO(new.read('SYSTEM/framework/telephony-common.jar'))) as jar:
        assert b'addLt013gPdpGatewayHostRoute' in jar.read('classes.dex'), 'G387 network fix removed'
    for n in new.namelist():
        if ('T311GnssStatus' in n or 'T311NtpTransportProbe' in n):
            raise SystemExit('G388 unauthorized diagnostic packaged: '+n)

# Reapply G387's strict G382-baseline new ARM32 helper, OEM TWRP/BOOT/RIL/audio,
# GNSS private Binder CIL, original fstab and 10+4 Java negative cases.
subprocess.run([sys.executable,str(folder/'stage_g387_xmm6262_route_target_files.py'),
                str(a.source),str(a.output),
                '--baseline',str(a.baseline_g382),
                '--android',str(a.android)],check=True)
with ZipFile(a.output) as done, ZipFile(a.source) as input_again:
    if done.read(oldfile)!=changed:
        raise SystemExit('G388 BCB /misc mutated while staging')
    for name in immutable:
        if done.read(name)!=input_again.read(name):
            raise SystemExit('G388 immutable binary changed during staging: '+name)

result={
 'status':'G388_OTA_P11_MISC_PLUS_G387_NETWORK_ROUTE_COMPILED_GUARDED',
 'physical_flash_performed':False,
 'new_vendor_fstab_sha256':hashlib.sha256(changed).hexdigest(),
 'old_vendor_fstab_sha256':hashlib.sha256(existing).hexdigest(),
 'selected_fstab_path':'/vendor/etc/fstab.lt013g',
 'android_misc_partition':'/dev/block/platform/soc/12550000.mmc/by-name/OTA',
 'factory_pit_OTA':'mmcblk0p11',
 'fs_mgr_mount_all_skips_raw_emmc':True,
 'TWRP_restart_physically_proven':False,
 'valid_G387_fixed_route_remains':True,
 'unchanged_critical_payloads':immutable,
 'source_target_files_sha256':hashlib.file_digest(a.output.open('rb'),'sha256').hexdigest(),
 'bytes':a.output.stat().st_size,
}
a.output.with_name('G388-BCB-RELEASE-GATE.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
