#!/usr/bin/env python3
"""SM-T311 G394: signed G389 physical factory eMMC bounds and REAL installed G379 OTA parity.

Input sectors correspond to read-only actual SM-T311 /sys/class/block
observations. No access to any raw eMMC device, no shell ADB command, no
TWRP/reboot/write. ZIP only, rejects overlap/out-of-range/unknown opcodes.
"""
import argparse
import hashlib
from pathlib import Path
import re
from zipfile import ZipFile

SIGNED_G389_SHA='339d7e2ac87cfdacd6957c92450fe2a503902ac334c6c53204f07474f344c09b'
SIGNED_G379_SHA='6bda8b1d12eb431cc31646e3b32f19f75bdb27ab4f0c69d3fb8dcffa3299a834'
# Read-only 2026-10-17 actual /sys/class/block/mmcblk0pN/size (512B sectors).
FACTORY_SECTORS={
    9:16384,10:16384,11:16384,12:16384,19:835584,20:4366336,21:25026560,
}
BLOCK=4096
SYSTEM_BYTES=FACTORY_SECTORS[20]*512
BOOT_BYTES=FACTORY_SECTORS[9]*512
def require(value, why):
    if not value: raise AssertionError(why)

def digest(b):
    return hashlib.sha256(b).hexdigest()

def ranges(encoded, capacity):
    v=encoded.split(',')
    require(len(v)>=3,'range too short')
    ints=[]
    for item in v:
        require(item.isdecimal(),'invalid range coordinate: '+item)
        ints.append(int(item))
    require(ints[0]>=2 and ints[0]%2==0 and 1+ints[0]==len(ints),'range pair count mismatch')
    pairs=list(zip(ints[1::2],ints[2::2]))
    require(len(pairs)>0,'empty range set')
    prev=0
    for i,(start,end) in enumerate(pairs):
        require(start<end,'empty/descending extent')
        require(start>=prev if i else start>=0,'overlapping/unsorted extents')
        require(end<=capacity,'range exceeds physical SYSTEM partition')
        prev=end
    return pairs

def transfer_contract(transfer, physical=SYSTEM_BYTES, expected_blocks=280977):
    require(physical>0 and physical%BLOCK==0, 'unaligned physical partition')
    lines=transfer.splitlines()
    require(len(lines)>4,'missing transfer directives')
    require(lines[:4]==['4',str(expected_blocks),'0','0'],
            'wrong transfer version/total blocks, or nonzero stash dependencies')
    physical_blocks=physical//BLOCK
    seen=[];written=0;zeroed=0;erased=0
    # FULL non-incremental OTA. All listed extents must fit and must not
    # overlap. No move/stash/free/bsdiff/imgdiff/source-image tricks.
    for line in lines[4:]:
        v=line.split()
        require(len(v)==2,'unexpected transfer command syntax')
        cmd,extent=v
        require(cmd in ('new','erase','zero'),'incremental/unknown or unsafe transfer opcode: '+cmd)
        ps=ranges(extent,physical_blocks)
        for lo,hi in ps:
            seen.append((lo,hi,cmd))
            if cmd=='new':written+=hi-lo
            elif cmd=='zero':zeroed+=hi-lo
            else:erased+=hi-lo
    require(written+zeroed==expected_blocks, 'declared total new+zero block count does not match actual')
    sorted_ranges=sorted(seen)
    for prev,cur in zip(sorted_ranges,sorted_ranges[1:]):
        require(prev[1]<=cur[0],'overlapping SYSTEM destination extents')
    require(written+zeroed+erased<=physical_blocks,'writes/erases exceed physical partition')
    require(max(e for _,e,_ in seen)<=physical_blocks,'last destination beyond SYSTEM')
    return {
        'partition_bytes':physical,
        'partition_4k_blocks':physical_blocks,
        'written_blocks':written,
        'new_data_bytes':written*BLOCK,
        'written_bytes':(written+zeroed)*BLOCK,
        'zero_blocks':zeroed,
        'erase_blocks':erased,
        'highest_block_end':max(e for _,e,_ in seen),
        'total_directives':len(lines)-4,
        'distinct_target_extents':len(seen),
    }

def zip_contract(current,original):
    for name in ('META-INF/com/google/android/update-binary',
                 'META-INF/com/google/android/updater-script',
                 'system.transfer.list'):
        require(current.read(name)==original.read(name),
                'signed G389 updater differs from physically SUCCESSFUL real G379: '+name)
    boot=current.read('boot.img')
    require(0<len(boot)<=BOOT_BYTES,'signed BOOT payload outside actual 8MiB partition')
    require(digest(boot)=='cad5e76faf372beff3243ec615924286096713c994a6e5a03d3231f69478e6fd',
            'signed BOOT not exact G379 already installed candidate')
    code=current.read('META-INF/com/google/android/updater-script').decode()
    require(code.count('block_image_update(')==1,'SYSTEM update unexpected count')
    require('"/dev/block/platform/dw_mmc/by-name/SYSTEM"' in code,'TWRP SYSTEM block alias wrong')
    require('"/dev/block/platform/dw_mmc/by-name/BOOT"' in code,'TWRP BOOT block alias wrong')
    require('package_extract_file("META-INF/t311-g379-check.sh"' in code,'signed preflight absent')
    require(code.index('"pre"')<code.index('block_image_update(')<
            code.index('package_extract_file("boot.img",')<code.index('"post"'),
            'wrong pre/SYSTEM/BOOT/post ordering')
    need={'system.new.dat.br','system.patch.dat','system.transfer.list'}
    require(need.issubset(set(current.namelist())),'missing essential transfer payload')
    require(current.getinfo('system.patch.dat').file_size==0,
            'incremental patch unexpected in full OTA')
    require(current.read('system.new.dat.br')[:4]!=b'\x00\x00\x00\x00',
            'invalid new data stream')
    return transfer_contract(current.read('system.transfer.list').decode())

def main():
    a=argparse.ArgumentParser()
    a.add_argument('--project',type=Path,required=True)
    opts=a.parse_args()
    p=opts.project/'artifacts'
    g389=p/'SM-T311-G389-ENFORCING-BCB-20261017/SM-T311-G389-GUARDED-GPS-ROUTE-BCB-SELINUX-SIGNED.zip'
    g379=p/'SM-T311-G379-LIVE-OTA-20261016/SM-T311-G379-GUARDED-BOOT-GPS-CARRIER-TWRP-SIGNED.zip'
    require(hashlib.file_digest(g389.open('rb'),'sha256').hexdigest()==SIGNED_G389_SHA,
            'candidate G389 not exact authorized signed artifact')
    require(hashlib.file_digest(g379.open('rb'),'sha256').hexdigest()==SIGNED_G379_SHA,
            'G379 base OTA not exact physically installed guarded artifact')
    with ZipFile(g389) as curr, ZipFile(g379) as original:
        result=zip_contract(curr,original)
    require(result['written_bytes']==1150881792,
            'previous TWRP physical install wrote a different number of SYSTEM bytes')
    require(result['written_blocks']==274149 and result['zero_blocks']==6828,
            'previous TWRP physical install wrote a different new/zero SYSTEM count')
    print('G394 SUCCESS: signed G389 has byte-exact static updater, updater script and transfer map of G379 physically installed in TWRP')
    print('G394 ACTUAL p9 BOOT=',BOOT_BYTES,'bytes, p20 SYSTEM=',SYSTEM_BYTES,
          'bytes, max4kblocks=',result['partition_4k_blocks'])
    print('G394 ',result)
    valid='4\n280977\n0\n0\nnew 2,0,280977\n'
    transfer_contract(valid)
    mutated=[
        ('out_of_physical_system','4\n280977\n0\n0\nnew 2,300000,580977\n'),
        ('one_past_physical_system','4\n280977\n0\n0\nnew 2,264816,545793\n'),
        ('overlap_new_and_erase','4\n280977\n0\n0\nnew 2,0,280977\nerase 2,280976,280980\n'),
        ('duplicate_new_write','4\n280977\n0\n0\nnew 2,0,280977\nnew 2,0,1\n'),
        ('wrong_declared_block_count','4\n280978\n0\n0\nnew 2,0,280977\n'),
        ('disallow_incremental_stash','4\n280977\n1\n1\nnew 2,0,280977\n'),
        ('reject_transfer_v3','3\n280977\n0\n0\nnew 2,0,280977\n'),
        ('unexpected_move_command','4\n280977\n0\n0\nnew 2,0,280977\nmove 2,0,1\n'),
        ('negative_extent','4\n280977\n0\n0\nnew 2,-1,280976\n'),
        ('descending_extent','4\n280977\n0\n0\nnew 2,280977,0\n'),
        ('bad_extent_tuple_count','4\n280977\n0\n0\nnew 3,0,280977\n'),
        ('overlap_inner_range','4\n280977\n0\n0\nnew 4,0,280977,4,5\n'),
        ('extra_transfer_operand','4\n280977\n0\n0\nnew 2,0,280977 extra\n'),
        ('unknown_block_method','4\n280977\n0\n0\npatch 2,0,280977\n'),
        ('out_of_bounds_erase','4\n280977\n0\n0\nnew 2,0,280977\nerase 2,545792,545793\n'),
    ]
    for label,mutant in mutated:
        try:transfer_contract(mutant)
        except AssertionError:print('PASS rejects unsafe transfer:',label)
        else:raise AssertionError('FAIL OPEN on '+label)
    print('G394 EXACT FACTORY EMMC+SUCCESSFUL G379 BLOCK OTA PARITY GATE PASS: 15/15 negative cases; NO DEVICE WRITES')

if __name__=='__main__':
    main()
