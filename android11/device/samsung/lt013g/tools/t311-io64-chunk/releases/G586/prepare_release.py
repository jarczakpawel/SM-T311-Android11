#!/usr/bin/env python3
"""Build verified G586 low-space USB transport from the exact official ZIP.

Host-only. Never touches an ADB device, real SYSTEM, BOOT, EFS, data,
or old G573 artifacts. Removes ONLY temporary image copies after success.
"""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import sys
from zipfile import ZipFile

TOP=Path('/home/debian/android/lineage-18.1-sm-t311')
ZIP=TOP/'out/target/product/lt013g/obj/PACKAGING/target_files_intermediates/lineage_lt013g-target_files-SMT311G586.zip'
OUT=Path('/home/debian/Projekty/samsung/artifacts/SM-T311-G586-VERIFIED-SYSTEM-20261020')
SCRIPTS=Path(__file__).resolve().parent
ZIP_SHA='5ac8e055d2ffa326a5b72e9590db6fbb556c3c61668fbd84ed174c56d6f00a34'
RAW_SHA='2b7f9571b14f3a85e325fd0ef7f9cf1c9fdd56f401444210dbc8ae2dd1a6bd09'
def sha256(path):
    with path.open('rb') as src:
        return hashlib.file_digest(src,'sha256').hexdigest()
if sha256(ZIP)!=ZIP_SHA:
    sys.exit('G586_FAIL_INTEGRITY_SOURCE_ZIP')
OUT.mkdir(parents=True,exist_ok=True)
sparse=OUT/'G586-ONLY-TEMP-system.sparse'
raw=OUT/'G586-ONLY-TEMP-system.raw'
try:
    with ZipFile(ZIP) as z:
        assert not any('T311GnssStatus' in n for n in z.namelist())
        assert hashlib.sha256(z.read('SYSTEM/vendor/lib/hw/gps.default.so')).hexdigest() == '2e326e700513cb86b79d6cd7ef77adb3d1b595c46e8b69916ea08ae5c7485cd9'
        with z.open('IMAGES/system.img') as src,sparse.open('wb') as dst:
            shutil.copyfileobj(src,dst,16*1024*1024)
    subprocess.run([str(TOP/'out/host/linux-x86/bin/simg2img'),str(sparse),str(raw)],check=True)
    if raw.stat().st_size!=2235564032 or sha256(raw)!=RAW_SHA:
        sys.exit('G586_FAIL_SPARSE_RAW_SHA')
    print('G586_SPARSE_CONVERT_RAW_SHA_PASS',flush=True)
    subprocess.run(['python3',str(SCRIPTS/'prepare_chunks.py'),str(raw),str(OUT),str(ZIP)],check=True)
    subprocess.run(['python3',str(SCRIPTS/'prepare_transport64.py'),str(OUT)],check=True)
    manifest=json.loads((OUT/'manifest.json').read_text())
    slices=json.loads((OUT/'transport64/slice_manifest.json').read_text())
    assert manifest['raw_system_sha256']==RAW_SHA
    assert slices['raw_sha256']==RAW_SHA
    assert len(manifest['parts'])==5 and len(slices['files'])==11
    assert all(0<f['size']<=64*1024*1024 for f in slices['files'])
    print('G586_TRANSPORT_PINNED_11_SLICES_VALIDATED',flush=True)
finally:
    for f in (raw,sparse):
        if f.is_file():
            f.unlink()
            print('G586_ONLY_TEMP_REMOVED',f.name,flush=True)
