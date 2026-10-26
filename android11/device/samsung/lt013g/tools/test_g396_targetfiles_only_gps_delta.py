#!/usr/bin/env python3
"""SM-T311 G396 guarded full target-files SHA/CRC release delta vs signed G389.

Fail closed on any unapproved file change: G396 may change only modern
gps.default ARM32 proxy + rebuilt SYSTEM, CACHE, USERDATA images and
date-dependent system/vendor build.prop. All factory kernel/BOOT/RECOVERY,
old OEM GPS/Radio/RIL/audio, compiled ART and SELinux are unchanged.
"""
import argparse
import hashlib
from pathlib import Path
import struct
from zipfile import ZipFile

NEW_GPS='SYSTEM/vendor/lib/hw/gps.default.so'
DELTA={
    NEW_GPS,
    'IMAGES/system.img',
    'IMAGES/cache.img',
    'IMAGES/userdata.img',
    'SYSTEM/build.prop',
    'SYSTEM/vendor/build.prop',
}
PROTECTED={
    'IMAGES/boot.img',
    'BOOTABLE_IMAGES/boot.img',
    'IMAGES/recovery.img',
    'BOOTABLE_IMAGES/recovery.img',
    'SYSTEM/vendor/etc/recovery.img',
    'RECOVERY/RAMDISK/etc/recovery.fstab',
    'SYSTEM/vendor/lib/libril.so',
    'SYSTEM/vendor/lib/t311-ril/libsec-ril.so',
    'SYSTEM/lib/libaudiohal.so',
    'SYSTEM/vendor/lib/hw/audio.primary.lt013g.so',
    'SYSTEM/vendor/etc/lt013g-gps-legacy/immutable/system/lib/hw/gps.default.so',
    'SYSTEM/vendor/etc/selinux/vendor_sepolicy.cil',
    'SYSTEM/vendor/etc/selinux/vendor_file_contexts',
    'SYSTEM/vendor/etc/fstab.lt013g',
    'SYSTEM/framework/telephony-common.jar',
    'SYSTEM/framework/arm/boot-telephony-common.art',
    'SYSTEM/framework/arm/boot-telephony-common.oat',
    'SYSTEM/framework/boot-telephony-common.vdex',
}
def need(x,msg):
    if not x:raise AssertionError(msg)

def only_allowed(before,after):
    need(set(before)==set(after),'G396 removed/inserted unexpected target file')
    changed={x for x in before if before[x]!=after[x]}
    need(changed==DELTA,'unexpected G396 targetfiles delta: '+str(sorted(changed^DELTA)))
    for n in PROTECTED:
        need(n in before and n in after, 'critical OEM file not present '+n)
        need(before[n]==after[n], 'critical OEM file mutated '+n)
    return sorted(changed)

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--project',type=Path,required=True)
    a=p.parse_args()
    path=a.project/'artifacts'
    previous=path/'SM-T311-G389-ENFORCING-BCB-20261017/G389-enforcing-bcb-target-files-fstab.zip'
    candidate=path/'SM-T311-G396-SUPL-NOIP-20261017/G396-full-guarded-target-files.zip'
    need(previous.is_file() and candidate.is_file(),'signed G389 and staged G396 targetfiles missing')
    with ZipFile(previous) as o, ZipFile(candidate) as n:
        old={f.filename:(f.CRC,f.file_size) for f in o.infolist()}
        new={f.filename:(f.CRC,f.file_size) for f in n.infolist()}
        delta=only_allowed(old,new)
        a=o.read(NEW_GPS)
        b=n.read(NEW_GPS)
        need(a!=b and len(b)>len(a),'new GPS binary not actually built from G396')
        need(b[:4]==b'\x7fELF' and b[4]==1 and b[5]==1,'G396 gps.default not 32bit little endian ELF')
        need(struct.unpack_from('<H',b,18)[0]==40,'G396 GNSS HAL not compiled ARM32')
        need(b'no-supl-ip-jni-null' in b,'G396 callback marker not in compiled proxy ELF')
        need('misc_block_device' in n.read('SYSTEM/vendor/etc/selinux/vendor_file_contexts').decode(),
             'G389 compiled p11 security fix lost')
        need('/misc emmc defaults defaults' in n.read('SYSTEM/vendor/etc/fstab.lt013g').decode(),
             'G388 active recovery BCB vendor fstab fix lost')
        need(b'addLt013gPdpGatewayHostRoute' in n.read('SYSTEM/framework/telephony-common.jar'),
             'G387 framework data route fix lost')
        for name in n.namelist():
            need('T311GnssStatus' not in name and 'T311NtpTransportProbe' not in name,
                 'test-only diagnostic shipped in user ROM')
        gps_sha=hashlib.sha256(b).hexdigest()
        boot_sha=hashlib.sha256(n.read('IMAGES/boot.img')).hexdigest()
        recovery_sha=hashlib.sha256(n.read('IMAGES/recovery.img')).hexdigest()
        need(boot_sha=='cad5e76faf372beff3243ec615924286096713c994a6e5a03d3231f69478e6fd',
             'unexpected boot image in G396')
        need(recovery_sha=='f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f',
             'unexpected factory TWRP image in G396')
        need(n.testzip() is None,'G396 targetfiles CRC mismatch')
    # Strict negative-gate mutations in *metadata only*: no real 1.1GB
    # archive copies or SYSTEM image rewrites, and no device operations.
    bad=[
        ('unexpected_fstab_delta','SYSTEM/vendor/etc/fstab.lt013g'),
        ('accidental_OEM_gps_change','SYSTEM/vendor/etc/lt013g-gps-legacy/immutable/system/lib/hw/gps.default.so'),
        ('broken_RIL_driver','SYSTEM/vendor/lib/t311-ril/libsec-ril.so'),
        ('incorrect_radio_telematics','SYSTEM/framework/telephony-common.jar'),
        ('accidental_BOOT','IMAGES/boot.img'),
        ('accidental_TWRP','IMAGES/recovery.img'),
        ('accidental_SELinux','SYSTEM/vendor/etc/selinux/vendor_sepolicy.cil'),
        ('old_bcb_policy_missing','SYSTEM/vendor/etc/selinux/vendor_file_contexts'),
    ]
    for label,filename in bad:
        fake=dict(new)
        crc,size=fake[filename]
        fake[filename]=(crc^1,size)
        try:only_allowed(old,fake)
        except AssertionError:print('PASS rejected dangerous targetfile delta:',label)
        else:raise AssertionError('FAIL OPEN '+label)
    fake=dict(new)
    fake.pop('RECOVERY/RAMDISK/etc/recovery.fstab')
    try:only_allowed(old,fake)
    except AssertionError:print('PASS rejected missing old TWRP fstab entry')
    else:raise AssertionError('FAIL OPEN missing recovery metadata')
    print('G396 GUARDED FULL TARGETFILES DELTA PASS: exactly six expected changes; 9/9 dangerous mutations rejected')
    print('G396 actual built ELF32 ARM GNSS HAL SHA256',gps_sha,'factory BOOT+TWRP unchanged, G387/G388/G389 retained, no OTA flashed')
    print('G396 target files changed entries:',delta)

if __name__=='__main__':main()
