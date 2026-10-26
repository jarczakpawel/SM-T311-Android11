#!/usr/bin/env python3
"""G382 fail-closed source+compiled policy checks, with no device writes."""
from pathlib import Path

root = Path(__file__).resolve().parents[2] / 'lt01-common/sepolicy/vendor'
# Device project layout: lt013g/tools -> device/samsung/lt013g -> device/samsung
assert root.is_dir(), root
node = 'genfscon binder /lt013g-gps u:object_r:lt013g_gps_private_binder_device:s0'
genfs = (root / 'genfs_contexts').read_text()
policy = (root / 'lt013g_g382_gps_private_binder.te').read_text()
legacy = (root / 'lt013g_g341_enforcing_hardware.te').read_text()

assert genfs.count(node) == 1, 'Missing or ambiguous isolated Binder genfs label'
assert 'type lt013g_gps_private_binder_device, dev_type;' in policy
assert 'allow lt013g_gps_legacy lt013g_gps_private_binder_device:chr_file { getattr setattr open read write ioctl };' in policy
assert 'allow lt013g_gps_legacy binder_device:chr_file mounton;' in policy
assert 'allow lt013g_gps_legacy binder_device:chr_file setattr' not in legacy
assert 'allow lt013g_gps_legacy binderfs:filesystem' not in policy
assert 'permissive lt013g_gps_legacy;' in legacy, 'Do not switch to Enforcing before full GNSS tests'

# This extra vendor label must never steal the three normal Android Binder nodes.
for basename in ('/binder', '/hwbinder', '/vndbinder'):
    assert ('genfscon binder ' + basename + ' ') not in genfs

compiled = Path(__file__).resolve().parents[4] / 'out/target/product/lt013g/obj/ETC/vendor_sepolicy.cil_intermediates/vendor_sepolicy.cil'
assert compiled.is_file(), compiled
cil = compiled.read_text()
assert '(genfscon binder /lt013g-gps (u object_r lt013g_gps_private_binder_device ((s0) (s0))))' in cil
assert '(allow lt013g_gps_legacy lt013g_gps_private_binder_device (chr_file (ioctl read write getattr setattr open)))' in cil
print('G382 GPS PRIVATE BINDER POLICY SOURCE+COMPILED CIL PASS')
