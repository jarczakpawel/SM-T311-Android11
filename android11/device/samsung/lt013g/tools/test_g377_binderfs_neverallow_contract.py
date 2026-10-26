#!/usr/bin/env python3
"""Prevent attempting forbidden direct vendor-domain binderfs filesystem mounts.

This is a host-only negative security regression test. It deliberately
rejects broadening AOSP Android11 neverallow rules. It does not mount, flash
or contact the physical SIM/tablet.
"""
from pathlib import Path
import re

device=Path(__file__).resolve().parents[1]
android=device.parents[2]
aosp=android/"system/sepolicy/private/domain.te"
policy=aosp.read_text()
match=re.search(r"neverallow\s*\{([^}]+)\}\s*\{\s*fs_type\s+-sdcard_type\s*\}\s*:\s*filesystem\s*\{([^}]+)\}",policy,re.S)
assert match,"Android11 global mount protection disappeared or parser needs review"
subjects,perms=match.groups()
assert "domain" in subjects
assert "-init" in subjects
assert "-lt013g_gps_legacy" not in subjects
assert "mount" in perms and "remount" in perms
assert "binderfs" in (android/"system/sepolicy/public/file.te").read_text()
launcher=(device/"gps-v195/legacy_launcher.cpp").read_text()
service=(device/"gps-v195/init.lt013g.gps-legacy.rc").read_text()
assert "namespace mnt" in service
assert "setupPrivateLegacyBinder(root)" in launcher
assert 'mount("binder", binderfs.c_str(), "binder", 0, nullptr)' in launcher
for base in [device/"sepolicy",android/"device/samsung/lt01-common/sepolicy"]:
    if not base.exists():continue
    for te in base.rglob("*.te"):
        for ln in te.read_text(errors="replace").splitlines():
            if not ln.lstrip().startswith("#"):
                assert not re.search(
                    r"allow\s+lt013g_gps_legacy\s+binderfs\s*:\s*filesystem\s*.*\bmount\b",ln),te
print("G377_NEVERALLOW_GUARD_PASS vendor_GNSS_cannot_mount_binderfs next=init_owned_private_binder_instance")
