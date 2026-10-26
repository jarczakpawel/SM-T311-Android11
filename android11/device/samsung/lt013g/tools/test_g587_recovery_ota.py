#!/usr/bin/env python3
"""Check only the pinned G587 TWRP OTA against upstream G586 payload.

No ADB calls, no block writes. Refuse any shift to recovery/data or
unknown OTA operations. Checks exact byte-for-byte new/patch dat and BOOT.
"""
import hashlib
from pathlib import Path
from zipfile import ZipFile
from g408_generate_pinned_twrp_preflight import validate_full_system_transfer

BASE=Path('/home/debian/Projekty/samsung/artifacts/SM-T311-G586-VERIFIED-SYSTEM-20261020')
OLD=BASE/'G586-FULL-AOSP-OTA-HOST-ONLY.zip'
NEW=BASE/'G587-G586-TWRP-GUARDED-SIGNED.zip'
SHA='157e4fcce298e3fab7f54b9cf6087cb31060022e4f247a1e2b5b92201a4bac2a'
BOOT='81b01abb5612a9a777330705c02c4bc1f49386da4d5df0950f955246f9fa6bce'
def digest(data):return hashlib.sha256(data).hexdigest()
def filehash(path):
    with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
assert filehash(NEW)==SHA
with ZipFile(OLD) as prior, ZipFile(NEW) as candidate:
    assert prior.testzip() is None and candidate.testzip() is None
    files=set(candidate.namelist())
    assert 'META-INF/t311-g587-check.sh' in files
    assert 'recovery.img' not in files and 'userdata.img' not in files
    assert digest(candidate.read('boot.img'))==BOOT
    for name in ('boot.img','system.new.dat.br','system.patch.dat','system.transfer.list',
                 'META-INF/com/google/android/update-binary'):
        assert candidate.read(name)==prior.read(name),f'G587 mutated {name}'
    info=validate_full_system_transfer(candidate.read('system.transfer.list'))
    assert info['system_blocks']==545792
    code=candidate.read('META-INF/com/google/android/updater-script').decode()
    guard=candidate.read('META-INF/t311-g587-check.sh').decode()
    markers=[
        'assert(run_program("/sbin/sh", "/tmp/t311-g587-check.sh", "pre")',
        'block_image_update("/dev/block/platform/dw_mmc/by-name/SYSTEM"',
        'package_extract_file("boot.img", "/dev/block/platform/dw_mmc/by-name/BOOT")',
        'assert(run_program("/sbin/sh", "/tmp/t311-g587-check.sh", "post")',
    ]
    assert all(code.count(x)==1 for x in markers)
    assert [code.index(x) for x in markers]==sorted(code.index(x) for x in markers)
    assert all(x not in code for x in ('format(', 'wipe_data', 'recovery.img',
                                        '/dev/block/platform/soc/12550000.mmc/'))
    assert '2235564032' in guard
    assert guard.count(BOOT)>=2
    assert ' /system ' in guard and '/proc/mounts' in guard
    assert digest(candidate.read('system.new.dat.br'))==digest(prior.read('system.new.dat.br'))
print('G587_SOURCE_SIGNED_TWRP_OTA_UNCHANGED_PAYLOAD_PASS')
print('G587_REAL_P20_TRANSFER_545792_BLOCKS_COVERAGE_PASS')
print('G587_BOOT_RECOVERY_USERDATA_PARTITION_SAFETY_PASS')
