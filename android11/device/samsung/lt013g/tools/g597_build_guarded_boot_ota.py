#!/usr/bin/env python3
from pathlib import Path
import g596_build_guarded_boot_ota as ota

ota.RELEASE = "G597"
ota.PREVIOUS = ota.PORT / "artifacts/G596-EXYNOS-DEVFREQ-ERROR-PROPAGATION/G596-BOOT-8MiB.img"
ota.OLD_BOOT = "aaad9251f03f2941c936af04b49e4f7b3849d16baa2a7bd4e13df997b1eb7e0e"
ota.ZIMAGE_SHA = "591ff57178686d237b1caa7f99792b8cdd82afcf87fad56d3100045e6feaa454"
ota.OUTPUT = ota.PORT / "artifacts/G597-EXACT-KERNEL-RELEASE"
ota.build()
