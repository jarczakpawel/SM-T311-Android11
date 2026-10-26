#!/usr/bin/env python3
"""G554: source-only assert post-mount zram activation and stable no-eMMC swap."""
from pathlib import Path
import re
root=Path("/home/debian/android/lineage-18.1-sm-t311/device/samsung/lt01-common/rootdir")
rc=(root/"init.lt013g.vendor.rc").read_text()
fstab=(root/"fstab.lt013g.vendor").read_text()
assert rc.count("swapon_all /vendor/etc/fstab.lt013g")==1
assert re.search(r"on fs\n\s+mount_all /vendor/etc/fstab.lt013g\n",rc)
assert not re.search(r"on fs\n(?:[^\n]*\n)*?\s+swapon_all",rc.split("on post-fs-data",1)[0])
post=rc.split("on post-fs-data\n",1)[1].split("\non ",1)[0]
assert "swapon_all /vendor/etc/fstab.lt013g" in post
active=[line for line in fstab.splitlines() if line.startswith("/dev/") and " swap " in line]
assert active == ["/dev/block/zram0 none swap defaults zramsize=805306368,swapprio=32758,wait"],active
assert "backing" not in active[0].lower()
source=Path("/home/debian/android/lineage-18.1-sm-t311/system/core/fs_mgr/fs_mgr.cpp").read_text()
assert "WaitForFile(ZRAM_CONF_DEV, 10s)" in source
assert "fflush(zram_fp.get())" in source
print("G554_INIT_FSTAB_FS_MGR_STATIC_PASS; physical zram not yet validated")
