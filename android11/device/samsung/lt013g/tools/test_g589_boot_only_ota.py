#!/usr/bin/env python3
"""Fail-closed validation of G589 target BOOT with no partition writes."""
import hashlib
import json
import re
from pathlib import Path
from zipfile import ZipFile
from g589_build_guarded_boot_ota import G582,OLD,NEW,SYSTEM,TWRP,UPDATER,GUARD,updater,guard,filehash

p=Path('/home/debian/Projekty/samsung/linux-7.3-port/artifacts/G582-SAFE-KERNEL-XARRAY-PMU-DVFS-DIAG')
z=p/'G589-G582-BOOT-ONLY-TWRP-SIGNED.zip'
m=json.loads((p/'G589-G582-BOOT-ONLY-MANIFEST.json').read_text())
assert z.is_file()
assert filehash(z)==m['signed_sha256']=='5ce92c5d587530ceed42d63bb4aa819ca025c82aa1af016e02c1921bdffc8109'
assert m['signed_size']==z.stat().st_size and m['status']=='SIGNED_HOST_BOOT_ONLY_NOT_FLASHED'
assert filehash(G582)==NEW and G582.stat().st_size==8388608
with ZipFile(z) as f:
    assert f.testzip() is None
    names=set(f.namelist())
    assert 'boot.img' in names and UPDATER in names and GUARD in names
    assert 'META-INF/com/google/android/update-binary' in names
    assert not any(re.search(r'(system|userdata|recovery|radio|modem)\.(?:img|new\.dat\.br)',n,re.I) for n in names)
    assert hashlib.sha256(f.read('boot.img')).hexdigest()==NEW
    assert f.read(UPDATER).decode()==updater()
    assert f.read(GUARD).decode()==guard()
    script=f.read(UPDATER).decode();gate=f.read(GUARD).decode()
    assert script.count('package_extract_file("boot.img"')==1
    assert script.count('run_program("/sbin/sh"')==2
    assert script.index('"pre"')<script.index('package_extract_file("boot.img"')<script.index('"post"')
    assert all(x not in script for x in ('block_image_update','wipe_data','format(','/data','/dev/block/mmcblk0p20','recovery.img'))
    assert all(x in gate for x in (OLD,NEW,SYSTEM,TWRP,'2235564032','8388608','/dev/block/platform/dw_mmc/by-name/BOOT'))
print('G589_SIGNED_BOOT_ONLY_OTA_PAYLOAD_PINNED_PASS')
print('G589_NO_SYSTEM_DATA_RECOVERY_RADIO_WRITE_PASS')
print('G589_PRE_POST_SOURCE_P20_P9_P10_GUARDS_PASS')
