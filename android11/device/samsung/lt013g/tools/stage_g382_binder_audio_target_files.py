#!/usr/bin/env python3
"""Require the new private GNSS binder policy before staging a guarded G382 OTA."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from zipfile import ZipFile

p = argparse.ArgumentParser()
p.add_argument('source', type=Path)
p.add_argument('output', type=Path)
a = p.parse_args()
policy_path = 'SYSTEM/vendor/etc/selinux/vendor_sepolicy.cil'
with ZipFile(a.source) as z:
    policy = z.read(policy_path)
    required = (
        b'(genfscon binder /lt013g-gps (u object_r lt013g_gps_private_binder_device',
        b'(allow lt013g_gps_legacy lt013g_gps_private_binder_device (chr_file (ioctl read write getattr setattr open)))',
    )
    for item in required:
        if item not in policy:
            raise SystemExit('G382 missing required private GNSS Binder policy: ' + repr(item))
    if b'(allow lt013g_gps_legacy binder_device_30_0 (chr_file (setattr)))' in policy:
        raise SystemExit('G382 old broad Binder setattr was not removed')
    digest = hashlib.sha256(policy).hexdigest()

# Reuse prior source-audited BOOT/TWRP/RIL/Audio checks and exact recovery fstab.
subprocess.run([sys.executable, str(Path(__file__).with_name('stage_g381_audio_target_files.py')),
                str(a.source), str(a.output)], check=True)
report_path = a.output.with_suffix('.json')
report = json.loads(report_path.read_text())
with ZipFile(a.output) as staged:
    if hashlib.sha256(staged.read(policy_path)).hexdigest() != digest:
        raise SystemExit('G382 vendor SELinux policy mutated during staging')
report.update(status='G382_AUDIO_PRIVATE_BINDER_TARGET_FILES_READY',
              vendor_sepolicy_sha256=digest,
              gps_private_binder_policy_present=True,
              gps_domain_still_permissive=True,
              physical_flash_performed=False)
report_path.write_text(json.dumps(report, indent=2) + '\n')
print('G382 SOURCE TARGET_FILES POLICY GATE PASS SHA256', digest)
