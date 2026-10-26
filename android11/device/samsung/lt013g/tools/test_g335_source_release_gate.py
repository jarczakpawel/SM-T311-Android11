#!/usr/bin/env python3
"""Unit-test source-release path gate using hypothetical injected Git paths.

Does not stage/add any private OEM files or mutate a Git index.
"""
from pathlib import Path
import importlib.util
import sys

p = Path(__file__).with_name("verify_source_release_gate.py")
spec = importlib.util.spec_from_file_location("g335_source_gate", p)
assert spec and spec.loader
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
cases = [
    ("gps-v195/immutable/system/bin/gpsd", True),
    ("gps-v195/legacy-rootfs.tar", True),
    ("gps-v195/legacy-rootfs-mutable.tar", True),
    ("firmware/s5p-mfc.fw", True),
    ("firmware/regulatory.db", True),
    ("firmware/regulatory.db.p7s", True),
    ("firmware/brcm/brcmfmac4334-sdio.samsung,t311.bin", True),
    ("firmware/brcm/brcmfmac4334-sdio.samsung,t311.txt", True),

    ("radio/prebuilt/libsec-ril.so", True),
    ("prebuilt/recovery-current.img", True),
    ("vendor/samsung/lt01-common/proprietary/system/lib/libMali.so", True),
    ("third_party/hardware/libsec-ril.so", True),
    ("stock/firmware/BCM4334B0.hcd", True),
    ("firmware/recovery-T311.img", True),
    ("gPs-v195/Immutable/SYSTEM/BIN/gpsd", True),
    ("../vendor/proprietary/sensitive.img", True),
    ("gps-v195/gps_proxy.c", False),
    ("radio/t311_audio_ril.c", False),
    ("tools/audit_oem_blob_release.py", False),
    ("tools/G334-OEM-RELEASE-INVENTORY-RESULTS.md", False),
    ("gps-v195/immutable-code.mk", False),
    ("gps-v195/legacy-rootfs.list", False),
    ("firmware/README.md", False),
    ("radio/Android.bp", False),
    ("tools/G333-SUPL-FRAMEWORK-CP-REPLACEMENT.md", False),
    ("device.mk", False),
]
for n, expected in cases:
    got = m.prohibited_path(n)
    assert bool(got) is expected, (n, got, expected)
    print("PASS" if expected else "PASS source allowed", n)
assert not m.assess(m.DEVICE)["offenders"]
kernel = m.assess(m.KERNEL)
offending_names = {
    "firmware/brcm/BCM4334B0.hcd",
    "firmware/brcm/BCM4334B0.samsung,t311.hcd",
    "firmware/brcm/brcmfmac4334-sdio.samsung,t311.bin",
    "firmware/brcm/brcmfmac4334-sdio.samsung,t311.txt",
    "firmware/regulatory.db", "firmware/regulatory.db.p7s",
    "firmware/s5p-mfc.fw",
}
assert all(any(name in violation for violation in kernel["offenders"])
           for name in offending_names), kernel["offenders"]
print("PASS source-only device Git index/history clean; kernel Git correctly BLOCKED on seven firmware paths")
print("G335 TOTAL", len(cases) + 2, "source-release gate cases PASS (kernel has intentional fail-closed result)")
