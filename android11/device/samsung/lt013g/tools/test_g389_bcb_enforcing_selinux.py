#!/usr/bin/env python3
"""SM-T311 G389: prove BCB writes require factory OTA p11 misc block label.

No actual partition reads/writes, no reboot/flash or permissive toggles.
Only source Android11 SELinux, by-name, and fail-closed synthetic mutants.
"""
import argparse
from pathlib import Path
import re

P11='/dev/block/mmcblk0p11'
EXACT=P11+' u:object_r:misc_block_device:s0'

def check(v,label):
    if not v: raise AssertionError(label)

def get_rules(ctx):
    out=[]
    for line in ctx.splitlines():
        line=line.strip()
        if line and not line.startswith('#'):
            cells=line.split()
            if len(cells)==2 and cells[0].startswith('/dev/block/'):
                out.append((cells[0],cells[1]))
    return out

def assert_safe(ctx):
    rules=get_rules(ctx)
    target=[(p,label) for p,label in rules if p==P11]
    check(target==[(P11,'u:object_r:misc_block_device:s0')],'p11 wrong/duplicate SELinux file_context')
    check(sum(label=='u:object_r:misc_block_device:s0' for _,label in rules)==1,
          'misc label must belong exclusively to physical p11')
    for name,expected in [
        ('/dev/block/mmcblk0p3','u:object_r:lt013g_efs_block_device:s0'),
        ('/dev/block/mmcblk0p13','u:object_r:radio_device:s0'),
        ('/dev/block/mmcblk0p19','u:object_r:cache_block_device:s0'),
        ('/dev/block/mmcblk0p21','u:object_r:userdata_block_device:s0'),
    ]:
        check((name,expected) in rules, 'sensitive node label modified: '+name)
    check(not any(re.search(r'[*(+?]|\[',p) and label=='u:object_r:misc_block_device:s0' for p,label in rules),
          'wildcard/sensitive-wide misc relabel prohibited')

def run(android):
    p=android/'device/samsung/lt01-common/sepolicy/vendor/file_contexts'
    ctx=p.read_text()
    assert_safe(ctx)
    aosp=android/'system/sepolicy/public'
    init=(aosp/'init.te').read_text()
    macros=(aosp/'global_macros').read_text()
    domain=(aosp/'domain.te').read_text()
    bcb=(android/'bootable/recovery/bootloader_message/bootloader_message.cpp').read_text()
    fs=(android/'device/samsung/lt01-common/rootdir/fstab.lt013g.vendor').read_text()
    check('allow init misc_block_device:blk_file w_file_perms;' in init,
          'AOSP init may no longer write labeled misc BCB')
    check('allow init dev_type:blk_file r_file_perms;' in init,
          'AOSP init cannot first read BCB')
    check('allow init block_device:blk_file w_file_perms;' not in init,
          'blanket raw block write accidentally added')
    check('neverallow { domain -kernel -init -recovery } block_device:blk_file { open read write };' in domain,
          'stock general block permission invariant changed')
    check("define(`w_file_perms', `{ open append write lock map }')" in macros,
          'write permissions macro unexpectedly changed')
    check('open(misc_blk_device.c_str(), O_WRONLY)' in bcb and
          'open(misc_blk_device.c_str(), O_RDONLY)' in bcb,
          'BCB requires distinct read then write access')
    check('/dev/block/platform/soc/12550000.mmc/by-name/OTA /misc emmc defaults defaults' in fs,
          'G388 actual active vendor /misc mapping removed')
    fixtures={
        'lost_p11': ctx.replace(EXACT,'',1),
        'wrong_raw_class': ctx.replace(EXACT,P11+' u:object_r:block_device:s0',1),
        'wrong_radio_partition': ctx.replace(EXACT,'/dev/block/mmcblk0p13 u:object_r:misc_block_device:s0',1),
        'wrong_efs_partition': ctx.replace(EXACT,'/dev/block/mmcblk0p3 u:object_r:misc_block_device:s0',1),
        'wrong_cache_partition': ctx.replace(EXACT,'/dev/block/mmcblk0p19 u:object_r:misc_block_device:s0',1),
        'wrong_userdata_partition': ctx.replace(EXACT,'/dev/block/mmcblk0p21 u:object_r:misc_block_device:s0',1),
        'duplicate_p11': ctx+'\n'+EXACT+'\n',
        'all_block_partitions_wildcard':ctx+'\n/dev/block/mmcblk0p.* u:object_r:misc_block_device:s0\n',
        'wrong_vendor_ota_alias':ctx.replace(EXACT,'/dev/block/by-name/OTA u:object_r:misc_block_device:s0',1),
        'type_outside_platform_allowlist':ctx.replace(EXACT,P11+' u:object_r:radio_device:s0',1),
    }
    for label,text in fixtures.items():
        try:assert_safe(text)
        except AssertionError: print('PASS rejected unsafe SELinux mutant:',label)
        else:raise AssertionError('FAIL-OPEN! Unsafe mutant '+label)
    print('G389 STOCK OTA P11 ENFORCING BCB GATE PASS: 10/10 negative fixtures + exact AOSP init read/write/neverallow and G388 fstab')
if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--android',type=Path,required=True)
    args=parser.parse_args()
    run(args.android)
