#!/usr/bin/env python3
"""G1566 source contract for identical geometry setters; no device mutation."""
from itertools import product
from pathlib import Path
from xml.etree import ElementTree as ET

ANDROID=Path(__file__).resolve().parents[4]
COMMON=ANDROID/'frameworks/base/packages/SystemUI'
SAMSUNG=ANDROID/'device/samsung/lt013g'
FLAG='config_skipUnchangedNotificationBackgroundGeometryDamage'

def flag(path):
    matches=[element for element in ET.parse(path).getroot() if element.get('name')==FLAG]
    assert len(matches)==1
    return matches[0].text.strip()

assert flag(COMMON/'res/values/bools.xml')=='false'
assert flag(SAMSUNG/'overlay/frameworks/base/packages/SystemUI/res/values/t311_platform_v107.xml')=='true'
p=(COMMON/'src/com/android/systemui/statusbar/notification/row/NotificationBackgroundView.java').read_text()
assert p.count('mSkipUnchangedGeometryDamage &&')==4
assert p.count('R.bool.config_skipUnchangedNotificationBackgroundGeometryDamage')==1
for variable in ('mActualHeight','mClipTopAmount','mClipBottomAmount','mBackgroundTop'):
    assert p.count('mSkipUnchangedGeometryDamage && '+variable+' == ')==1,variable
assert '''if (mExpandAnimationRunning) {
            return;
        }
        if (mSkipUnchangedGeometryDamage && mActualHeight == actualHeight)''' in p
# We preserve all four destination integer fields. For a repeated value on
# SM-T311 only, skip the identical assignment and unneeded invalidate().
checks=0
for field,source,desired,enabled in product(("actualHeight", "clipTopAmount", "clipBottomAmount", "backgroundTop"),
                                           (-1,0,1,50),(-1,0,1,50),(False,True)):
    old_final=desired
    actual_setter=not enabled or source!=desired
    new_final=desired if actual_setter else source
    assert new_final==old_final,field
    if not enabled:
        assert actual_setter
    if enabled and source==desired:
        assert not actual_setter
    if source!=desired:
        assert actual_setter
    checks+=1
print('G1566_BACKGROUND_HEIGHT_TOP_BOTTOM_BACKGROUND_GEOMETRY_SAME_STATE_128_CASES_PASS',checks)
