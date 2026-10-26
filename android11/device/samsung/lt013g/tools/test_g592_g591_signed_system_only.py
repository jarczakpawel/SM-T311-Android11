#!/usr/bin/env python3
"""Static checks for G592 signed SYSTEM-only TWRP OTA; no ADB or writes."""
from pathlib import Path
from zipfile import ZipFile
import hashlib

ART=Path('/home/debian/Projekty/samsung/artifacts/SM-T311-G591-CODEC2-FMQ-SELINUX-20261020')
SOURCE=ART/'G591-AOSP-FULL-OTA-HOST-ONLY.zip'
SIGNED=ART/'G592-G591-TWRP-SIGNED-SYSTEM-ONLY.zip'
SRC_SHA='b502076d0dfa77fbbe20cb056c27bd613c181660239fbe20e5d74e6c15c4ea4c'
SIGNED_SHA='86034f263fe97232d2fdf1ce8f496c113185e20ba28703f82f285eca9d6b7391'
GUARD='META-INF/t311-g592-system-only-prepost.sh'
UPDATER='META-INF/com/google/android/updater-script'
def filehash(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def ziphash(z,name):
    with z.open(name) as f:
        h=hashlib.sha256()
        while True:
            b=f.read(8*1024*1024)
            if not b: break
            h.update(b)
    return h.hexdigest()
assert filehash(SOURCE)==SRC_SHA
assert filehash(SIGNED)==SIGNED_SHA
with ZipFile(SOURCE) as x,ZipFile(SIGNED) as z:
    assert z.testzip() is None
    assert 'boot.img' not in z.namelist()
    assert 'recovery.img' not in z.namelist()
    assert 'userdata.img' not in z.namelist()
    assert 'radio.img' not in z.namelist()
    assert GUARD in z.namelist()
    # signapk.jar -w uses Android recovery's whole-ZIP signing format:
    # signature is in the archive COMMENT (as in physically accepted G587).
    # META-INF/*.RSA is not required for this OTA signing mode.
    assert len(z.comment) >= 100
    for member in ('system.new.dat.br','system.patch.dat','system.transfer.list',
                   'META-INF/com/google/android/update-binary'):
        assert ziphash(x,member)==ziphash(z,member)
    from g408_generate_pinned_twrp_preflight import validate_full_system_transfer
    ranges=validate_full_system_transfer(z.read('system.transfer.list'))
    assert ranges['system_blocks']==545792
    s=z.read(UPDATER).decode()
    assert s.count('block_image_update(')==1
    assert s.count('/dev/block/platform/dw_mmc/by-name/SYSTEM')==1
    assert 'boot.img' not in s
    assert '/dev/block/platform/soc/12550000.mmc' not in s
    assert not any(term in s for term in ('format(', 'wipe_data', '/dev/block/mmcblk0p9'))
    assert s.index('"pre"')<s.index('block_image_update(')<s.index('"post"')
    g=z.read(GUARD).decode()
    for token in ('/dev/block/mmcblk0p20','/dev/block/mmcblk0p9',
                  'f7c5091dcba5cbbf012d9eab5ac4a505834a813e272442d8ad8c00c24ae6ee65',
                  'f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f',
                  '2b7f9571b14f3a85e325fd0ef7f9cf1c9fdd56f401444210dbc8ae2dd1a6bd09',
                  '229e1a8cbf037fe68cfa8d190771f13c0207b46c0c31f874d0af656ebbf31ab1'):
        assert token in g
print('G592_SIGNED_SYSTEM_ONLY_OTA_HOST_VERIFIED')
print('G592_SYSTEM_TRANSFER_FULL_COVERAGE_545792_BLOCKS')
print('G592_PAYLOAD_BYTE_IDENTICAL_TO_AOSP_AND_BOOT_NOT_TOUCHED')
print('G592_OLD_G586_NEW_G591_AND_G582_KERNEL_GUARDS_PASS')
