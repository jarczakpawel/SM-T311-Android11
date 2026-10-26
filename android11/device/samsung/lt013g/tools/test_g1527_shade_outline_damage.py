#!/usr/bin/env python3
"""G1527 source contract and exhaustive stateless clipping transition proof."""
from pathlib import Path
from xml.etree import ElementTree as ET
from itertools import product

A = Path(__file__).resolve().parents[4]
FRAME = A / 'frameworks/base'
DEV = A / 'device/samsung/lt013g'
FLAG = 'config_skipRedundantShadeOutlineDamage'


def cfg(file):
    nodes = [n for n in ET.parse(file).getroot() if n.get('name') == FLAG]
    assert len(nodes) == 1
    return nodes[0].text.strip()

assert cfg(FRAME / 'packages/SystemUI/res/values/bools.xml') == 'false'
assert cfg(DEV / 'overlay/frameworks/base/packages/SystemUI/res/values/t311_platform_v107.xml') == 'true'
source = (FRAME / 'packages/SystemUI/src/com/android/systemui/statusbar/notification/stack/NotificationStackScrollLayout.java').read_text()
view = (FRAME / 'core/java/android/view/View.java').read_text()
assert 'private final boolean mSkipRedundantOutlineDamage;' in source
assert 'mSkipRedundantOutlineDamage = res.getBoolean(' in source
assert 'if (!mSkipRedundantOutlineDamage || getClipToOutline() != clipToOutline) {' in source
assert 'setClipToOutline(clipToOutline);' in source
assert 'public void setClipToOutline(boolean clipToOutline) {\n        damageInParent();' in view
# For all permutations of old state, desired state and opt-in: final clipping
# state is identical to AOSP; the only difference is skipping a redundant
# parent damage for the same state on the single opted-in Samsung model.
checks = 0
for old, new, enabled in product((False, True), repeat=3):
    original_end = new
    patched_invokes_setter = (not enabled) or old != new
    patched_end = new if patched_invokes_setter else old
    assert patched_end == original_end
    if not enabled:
        assert patched_invokes_setter
    if enabled and old == new:
        assert not patched_invokes_setter
    if old != new:
        assert patched_invokes_setter
    checks += 1
print('G1527_VIEW_OUTLINE_PARENT_DAMAGE_CONTRACT_8_CLIP_TRANSITIONS_PASS', checks)
