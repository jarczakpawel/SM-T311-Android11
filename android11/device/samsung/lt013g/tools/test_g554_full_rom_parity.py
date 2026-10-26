#!/usr/bin/env python3
"""Fail-closed G554 target-files check vs physical verified G551 payload.
No tablet writes; requires a freshly generated ARM32 target_files ZIP.
"""
from pathlib import Path
from zipfile import ZipFile
from hashlib import sha256

p=Path("/home/debian/android/lineage-18.1-sm-t311/out/target/product/lt013g/"
       "obj/PACKAGING/target_files_intermediates/lineage_lt013g-target_files-eng.debian.zip")
ref={
"IMAGES/boot.img": "81b01abb5612a9a777330705c02c4bc1f49386da4d5df0950f955246f9fa6bce",
"SYSTEM/vendor/lib/t311-ril/libsec-ril.so": "32ed7e04b92b8b3eaa94df3975375420f1b372ae4c379b5baae809b58c7a7157",
"SYSTEM/vendor/lib/hw/audio.primary.lt013g.so": "636743a098acb4ab31bad39adb3c46875c461d4b313f5fddb6c2106318224cfa",
"SYSTEM/vendor/lib/hw/gps.default.so": "8cba224bb2f77a4d829b6ff7a4f1da1461357b0cad3eba9d704ae7e56e88db5b",
"SYSTEM/lib/libfuse.so": "f096f3952bf8a91beb88a2b02ee0e0ffacb385e254029751f2d12d06c024230a",
"SYSTEM/vendor/lib/egl/libEGL_mesa.so": "01a219dc07c8b5ba752d02f8fed1ea5bedd92fe45292eaa082be8fb5a8ce5611",
"SYSTEM/vendor/lib/egl/libGLESv2_mesa.so": "10206f8df94e1e36ede21ba0577503007603d4d6eeb5be1e8e7316604878554f",
"SYSTEM/vendor/lib/libgallium_dri.so": "04148b6de255b5c563695a00f1517a788d1923d0ed825e5be18c1f069b20ae76",
}
with ZipFile(p) as z:
    names=set(z.namelist())
    for f,h in ref.items():
        assert f in names,f"missing {f}"
        got=sha256(z.read(f)).hexdigest()
        assert got==h,(f,got,h)
        print("PASS G551_PAYLOAD_PARITY",f)
    # Old/original G551 image already contains the signed-key authenticated
    # libfuse abi, and G554 modifies only init scheduling + diagnostic fs_mgr.
    rc=z.read("SYSTEM/vendor/etc/init/hw/init.lt013g.rc").decode()
    assert rc.count("swapon_all /vendor/etc/fstab.lt013g")==1
    early,late=rc.split("on post-fs-data\n",1)
    assert "swapon_all" not in early
    assert "swapon_all /vendor/etc/fstab.lt013g" in late
    fstab=z.read("SYSTEM/vendor/etc/fstab.lt013g").decode()
    assert "/dev/block/zram0 none swap defaults zramsize=805306368,swapprio=32758,wait" in fstab
    assert "SYSTEM/vendor/lib/egl/libGLES_mesa.so" not in names
    print("PASS G554_LATE_ZRAM_AND_MESA26_NO_G551_REGRESSION")
print("PASS G554_GOLDEN_PAYLOADS_SOURCE_ONLY_PHYSICAL_TEST_REQUIRED")
