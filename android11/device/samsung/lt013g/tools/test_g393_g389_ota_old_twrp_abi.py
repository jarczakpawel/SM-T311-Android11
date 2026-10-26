#!/usr/bin/env python3
"""G393: genuine G389 signed updater binary vs measured old SM-T311 TWRP ABI.

Read-only host test. Never executes ARM update-binary or modifies a block
device. Tests compiled ELF headers, static library independence, signed
updater-script order and exact Samsung by-name recovery mappings. Does not
prove physical TWRP execution or flash success.
"""
import argparse
import hashlib
from pathlib import Path
import re
import struct
from zipfile import ZipFile

from test_g390_twrp_preflight_offline import recovery_files, TWRP_HASH, G389_HASH

def need(cond, reason):
    if not cond:
        raise AssertionError(reason)

def u16(b, off):
    return struct.unpack_from('<H',b,off)[0]

def u32(b, off):
    return struct.unpack_from('<I',b,off)[0]

def abi(data, label):
    need(data[:4]==b'\x7fELF',label+': not ELF')
    need(data[4]==1,label+': not ELF32')
    need(data[5]==1,label+': not little endian')
    need(u16(data,18)==40,label+': not ARM')
    need((u32(data,36)&0xff000000)==0x05000000,label+': not EABI5')
    need((u32(data,36)&0x600)==0x200,label+': float ABI differs')
    need(u16(data,42)==32,label+': malformed program-header stride')
    phoff=u32(data,28)
    phnum=u16(data,44)
    need(0<phnum<=32 and phoff+phnum*32<=len(data),
         label+': malformed ELF program headers')
    ptypes=[u32(data,phoff+i*32) for i in range(phnum)]
    need(1 in ptypes,label+': no PT_LOAD')
    return ptypes

def recovery_props_contract(text):
    found={}
    for line in text.splitlines():
        if line.startswith('#') or '=' not in line:
            continue
        key,value=line.split('=',1)
        if key.startswith('ro.'):
            need(key not in found,'recovery default.prop duplicate '+key)
            found[key]=value
    for key,value in (
        ('ro.product.device','lt013g'),
        ('ro.build.product','lt013g'),
        ('ro.product.model','SM-T311'),
        ('ro.build.version.sdk','25'),
    ):
        need(found.get(key)==value,
             '2017 measured TWRP device/model/SDK mismatch '+key)
    return found

def contract(binary,twrp,updater,guard,boot,model="lt013g"):
    ph=abi(binary,'G389 updater')
    need(u16(binary,16)==2,'G389 updater must be ET_EXEC')
    need(2 not in ph,'G389 updater has dynamic linker section PT_DYNAMIC')
    need(3 not in ph,'G389 updater needs interpreter PT_INTERP')
    oldph=abi(twrp,'actual old TWRP')
    need(3 in oldph,'old TWRP control ELF unexpectedly no interpreter')
    need('/sbin/linker' in twrp.decode('latin1',errors='ignore'),
         'real old TWRP interpreter unavailable')
    need('getprop("ro.product.device") == "lt013g"' in updater,
         'G389 device guard no longer permits exact SM-T311 build')
    need('getprop("ro.product.device") == "lt013gxx"' in updater,
         'G389 device guard no longer permits Samsung locale variant')
    need('getprop("ro.product.device") == "SM-T311"' in updater,
         'G389 device guard no longer permits stock recovery identity')
    need('package_extract_file("META-INF/t311-g379-check.sh", "/tmp/t311-g379-check.sh")' in updater,
         'G389 physical BOOT guard extraction absent')
    need(updater.count('block_image_update(')==1,
         'G389 SYSTEM transfer missing or repeated')
    need(updater.count('package_extract_file("boot.img",')==1,
         'G389 BOOT update missing or repeated')
    need('block_image_update("/dev/block/platform/dw_mmc/by-name/SYSTEM"' in updater,
         'G389 updater targets wrong old TWRP SYSTEM alias')
    need('package_extract_file("boot.img", "/dev/block/platform/dw_mmc/by-name/BOOT")' in updater,
         'G389 updater targets wrong old TWRP BOOT alias')
    ordered=[
        'assert(run_program("/sbin/sh", "/tmp/t311-g379-check.sh", "pre")',
        'block_image_update(',
        'package_extract_file("boot.img",',
        'assert(run_program("/sbin/sh", "/tmp/t311-g379-check.sh", "post")',
    ]
    need(all(marker in updater for marker in ordered),
         'missing pre/post guard or SYSTEM/BOOT transaction')
    begin,middle,endboot,last=(updater.index(marker) for marker in ordered)
    need(begin < middle < endboot < last,'G389 writes before authorization or skips final BOOT verification')
    for token in ('format(','wipe_data','recovery.img','/dev/block/mmcblk0p11','/dev/block/mmcblk0p10'):
        need(token not in updater,'G389 unexpectedly writes sensitive partition or formats')
    need('bs=2048 count=3339' in guard,'guard BOOT prefix length mismatch')
    need(hashlib.sha256(boot).hexdigest()=='cad5e76faf372beff3243ec615924286096713c994a6e5a03d3231f69478e6fd',
         'unexpected G389 BOOT contents')

def main():
    pa=argparse.ArgumentParser()
    pa.add_argument('--project',type=Path,required=True)
    a=pa.parse_args()
    proj=a.project
    rec=proj/'device/backups/v111-existing-recovery-20260906-135425/RECOVERY-current-8MiB.img'
    zpath=proj/'artifacts/SM-T311-G389-ENFORCING-BCB-20261017/SM-T311-G389-GUARDED-GPS-ROUTE-BCB-SELINUX-SIGNED.zip'
    need(hashlib.file_digest(zpath.open('rb'),'sha256').hexdigest()==G389_HASH,'not approved signed G389 OTA')
    need(hashlib.file_digest(rec.open('rb'),'sha256').hexdigest()==TWRP_HASH,'not real measured old TWRP')
    files=recovery_files(rec)
    need('sbin/twrp' in files,'old TWRP native executable absent')
    need('default.prop' in files,'actual measured TWRP default.prop absent')
    old=files['sbin/twrp'][1]
    props=files['default.prop'][1].decode()
    recovery_props_contract(props)
    print('PASS actual measured TWRP recovery default.prop: lt013g/SM-T311 SDK25 signed G389 model whitelist')
    prop_mutants=[
        ('wrong_old_recovery_identity', props.replace('ro.product.device=lt013g','ro.product.device=lt01wifi').replace('ro.build.product=lt013g','ro.build.product=lt01wifi')),
        ('wrong_old_recovery_model',props.replace('ro.product.model=SM-T311','ro.product.model=SM-T310')),
    ]
    for label,mutant in prop_mutants:
        need(mutant != props,'missing recovery property mutation '+label)
        try:recovery_props_contract(mutant)
        except AssertionError: print('PASS rejected incompatible measured TWRP property mutant:',label)
        else: raise AssertionError('FAIL OPEN '+label)
    with ZipFile(zpath) as zipf:
        names=zipf.namelist()
        need(len(names)==len(set(names)),'duplicate signed ZIP member names')
        b=zipf.read('META-INF/com/google/android/update-binary')
        u=zipf.read('META-INF/com/google/android/updater-script').decode()
        g=zipf.read('META-INF/t311-g379-check.sh').decode()
        boot=zipf.read('boot.img')
    contract(b,old,u,g,boot)
    print('PASS genuine signed G389 OTA updater: ARM32 little-endian EABI5, statically linked ET_EXEC with no PT_INTERP/DYNAMIC')
    print('PASS actual SHA-measured TWRP: ARM32 little-endian EABI5, /sbin/linker, no G389 user-library dependency')
    bad=[
        ('updater_64bit',bytes(b[:4])+b'\x02'+b[5:],old,u,g,boot),
        ('updater_big_endian',bytes(b[:5])+b'\x02'+b[6:],old,u,g,boot),
        ('wrong_cpu_x86',b[:18]+struct.pack('<H',3)+b[20:],old,u,g,boot),
        ('updater_EABI4',b[:39]+b'\x04'+b[40:],old,u,g,boot),
        ('updater_wrong_float_abi',b[:36]+struct.pack('<I',(u32(b,36)&~0x600)|0x400)+b[40:],old,u,g,boot),
        ('updater_dynamic_interpreter',b[:u32(b,28)]+struct.pack('<I',3)+b[u32(b,28)+4:],old,u,g,boot),
        ('updater_dynamic_dependencies',b[:u32(b,28)]+struct.pack('<I',2)+b[u32(b,28)+4:],old,u,g,boot),
        ('TWRP_wrong_arch',b,old[:18]+struct.pack('<H',62)+old[20:],u,g,boot),
        ('target_wrong_partition',b,old,u.replace('/by-name/SYSTEM','/by-name/EFS'),g,boot),
        ('wrong_boot_target',b,old,u.replace('/by-name/BOOT','/by-name/RECOVERY'),g,boot),
        ('lost_pre_guard',b,old,u.replace('t311-g379-check.sh", "pre"','t311-g379-check.sh", "ignored"'),g,boot),
        ('lost_post_guard',b,old,u.replace('t311-g379-check.sh", "post"','t311-g379-check.sh", "ignored"'),g,boot),
        ('lost_signed_guard',b,old,u.replace('META-INF/t311-g379-check.sh','META-INF/unverified-check.sh'),g,boot),
        ('wrong_check_length',b,old,u,g.replace('count=3339','count=3000'),boot),
        ('modified_new_boot',b,old,u,g,boot[:-1]+bytes([boot[-1]^1])),
    ]
    for label,bb,tt,uu,gg,bo in bad:
        try:
            contract(bb,tt,uu,gg,bo)
        except AssertionError:
            print('PASS rejected unsafe ABI/updater mutant:',label)
        else:
            raise AssertionError('FAIL OPEN '+label)
    print('G393 OLD TWRP G389 OTA ABI GATE PASS: 15 ELF/guard + 2 recovery property negative cases = 17/17; NO execution or block writes')

if __name__=='__main__':
    main()
