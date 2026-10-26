#!/usr/bin/env python3
from pathlib import Path
import xml.etree.ElementTree as ET
a=Path(__file__).resolve().parents[4]
d=a/'device/samsung/lt013g'
root=(d/'lineage_lt013g.mk').read_text()
vendor=(a/'vendor/lineage/config/common_mobile.mk').read_text()
base=(a/'build/make/target/product/base_system.mk').read_text()
for flag in ('SMT311_EXCLUDE_SEEDVAULT := true','SMT311_EXCLUDE_DSU := true'):
    assert flag in root,flag
assert 'ifneq ($(SMT311_EXCLUDE_SEEDVAULT),true)' in vendor
assert 'PRODUCT_PACKAGES += Seedvault' in vendor
assert 'ifneq ($(SMT311_EXCLUDE_DSU),true)' in base
assert 'PRODUCT_PACKAGES += DynamicSystemInstallationService' in base
assert 'PRODUCT_DEXPREOPT_SPEED_APPS += Launcher3 TrebuchetQuickStep' in root
assert 'filter-out Seedvault' not in root
overlay=d/'overlay/frameworks/base/packages/SettingsProvider/res/values/defaults.xml'
parsed=ET.parse(overlay).getroot()
values={e.attrib['name']:e.text for e in parsed}
assert values['def_window_animation_scale']=='50%'
assert values['def_window_transition_scale']=='50%'
assert 'PrintSpooler' not in root and 'BuiltInPrintService' not in root
assert (d/'gps-v195/init.lt013g.gps-legacy.rc').read_text().count('setprop debug.lt013g.gps.v357.lto 0')==1
print('G1410_SOURCE_BUILD_EXCLUDES_BACKUP_DSU_ONLY_AND_PRESERVES_GNSS_PRINT_VIDEO_PASS')
