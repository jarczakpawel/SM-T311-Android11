#!/usr/bin/env python3
"""G388 SM-T311 reboot recovery /misc fs_mgr-selected fstab regression.

No raw BCB read and NO /misc partition writes. Tests real AOSP source
ReadDefaultFstab/GetFstabPath/init recovery BCB, Samsung OTA PIT identity,
and fail-closed mutation fixtures, not a fabricated physical reboot success.
"""
import argparse
from pathlib import Path
import re

MISC='/dev/block/platform/soc/12550000.mmc/by-name/OTA /misc emmc defaults defaults'
OTA='/dev/block/platform/soc/12550000.mmc/by-name/OTA'
def require(c,why):
    if not c: raise AssertionError(why)

def parsed(fstab):
    rows=[]
    for line in fstab.splitlines():
        s=line.strip()
        if not s or s.startswith('#'):continue
        parts=s.split()
        require(len(parts)>=5,'fstab row wrong field count')
        rows.append(parts)
    return rows

def verify_vendor_fstab(text):
    rows=parsed(text)
    misc=[r for r in rows if r[1]=='/misc']
    require(len(misc)==1,'/misc entry must be unique')
    require(misc[0]==MISC.split(),'wrong SM-T311 OTA raw BCB mapping or flags')
    require(sum(r[0]==OTA for r in rows)==1,'OTA aliased twice')
    require(len(rows)==4,'unexpected Android vendor fstab mount graph change')
    require({r[1] for r in rows}=={'/efs','/cache','/data','/misc'},
            'lost required exact factory mounts')
    require(all(r[2]=='emmc' for r in misc),'BCB must never be interpreted as filesystem')
    return rows

def test(android):
    root=android/'device/samsung'
    common=root/'lt01-common/rootdir'
    fstab=(common/'fstab.lt013g.vendor').read_text()
    ramdisk=(common/'fstab.lt013g').read_text()
    recovery=(root/'lt013g/rootdir/fstab.smdk4x12').read_text()
    fs_mgr=(android/'system/core/fs_mgr/fs_mgr_fstab.cpp').read_text()
    init=(android/'system/core/init/reboot.cpp').read_text()
    bcb=(android/'bootable/recovery/bootloader_message/bootloader_message.cpp').read_text()
    mountall=(android/'system/core/fs_mgr/fs_mgr.cpp').read_text()
    require(MISC in fstab,'vendor fstab missing OTA /misc')
    require(MISC in ramdisk,'original ramdisk BCB mapping lost')
    require('/dev/block/platform/dw_mmc/by-name/OTA' in recovery and
            ' /misc ' in recovery,'original factory recovery OTA BCB mapping changed')
    require('for (const char* prefix : {"/odm/etc/fstab.", "/vendor/etc/fstab.", "/fstab."})' in fs_mgr,
            'Android GetFstabPath precedence changed')
    require('default_fstab_path = GetFstabPath();' in fs_mgr,
            'Android normal boot fstab selection changed')
    require('if (entry.mount_point == "/misc")' in bcb and
            'return entry.blk_device;' in bcb,
            'Android BCB blk_device derived from /misc fstab no longer')
    require('if (std::string err; !write_bootloader_message(boot, &err)) {' in init and
            'reboot_target == "recovery"' in init and
            'LOG(ERROR) << "Failed to set bootloader message: "' in init,
            'Android recovery path BCB must fail before reboot if /misc missing')
    require('current_entry.fs_type == "swap" || current_entry.fs_type == "emmc"' in mountall,
            'emmc BCB must be skipped during mount_all (no OTA mounting)')
    verify_vendor_fstab(fstab)
    # Detect diverse regressions without kernel writes, modem access or GPS fixes.
    fixtures={
      'BCB_removed': fstab.replace(MISC,'',1),
      'wrong_PIT_partition':fstab.replace('/by-name/OTA /misc','/by-name/CACHE /misc'),
      'wrong_old_kernel_path':fstab.replace(OTA,'/dev/block/platform/dw_mmc/by-name/OTA'),
      'wrong_BCB_fs':fstab.replace('/misc emmc defaults defaults','/misc ext4 defaults defaults'),
      'BCB_would_be_mounted':fstab.replace('/misc emmc defaults defaults','/misc emmc defaults wait,check'),
      'duplicate_BCB':fstab+'\n'+MISC+'\n',
      'missing_EFS':fstab.replace('/efs ','/legacy-efs ',1),
      'missing_DATA':fstab.replace('/data ','/userdata ',1),
      'lost_cache':fstab.replace('/cache ','/legacy-cache ',1),
      'two_aliases':fstab+'\n'+OTA+' /another emmc defaults defaults\n',
    }
    for name,f in fixtures.items():
        try:verify_vendor_fstab(f)
        except AssertionError:
            print('PASS G388 deliberately invalid fstab rejected:',name)
            continue
        raise AssertionError('FAIL OPEN G388 fixture '+name)
    print('G388 SOURCE FSTAB TEST PASS: 10 negative cases; AOSP selects vendor; init BCB lookup; factory OTA p11; emmc skipped')
def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--android',type=Path,required=True)
    args=parser.parse_args()
    test(args.android)

if __name__=='__main__':main()
