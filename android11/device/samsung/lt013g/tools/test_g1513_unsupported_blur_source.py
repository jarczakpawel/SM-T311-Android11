#!/usr/bin/env python3
"""G1513: fail-closed source/overlay contract for disabled unsupported blur."""
from pathlib import Path
from xml.etree import ElementTree as ET

ROOT = Path(__file__).resolve().parents[4]
AOSP = ROOT / "frameworks/base/packages/SystemUI"
DEV = ROOT / "device/samsung/lt013g"
FLAG = "config_skipUnsupportedWindowBlurAnimations"

def value(p):
    nodes = [n for n in ET.parse(p).getroot() if n.get("name") == FLAG]
    assert len(nodes) == 1, (p, nodes)
    assert nodes[0].text in ("true", "false")
    return nodes[0].text

assert value(AOSP / "res/values/bools.xml") == "false"
assert value(DEV / "overlay/frameworks/base/packages/SystemUI/res/values/t311_platform_v107.xml") == "true"
blur = (AOSP / "src/com/android/systemui/statusbar/BlurUtils.kt").read_text()
depth = (AOSP / "src/com/android/systemui/statusbar/NotificationShadeDepthController.kt").read_text()
assert 'skipUnsupportedBlurAnimations && !supportsBlursOnWindows()' in blur
assert 'if (suppressUnsupportedBlurAnimations()) {\n            return 0' in blur
assert 'newRadius == 0 && radius == 0 && !springAnimation.isRunning' in depth
assert 'blurUtils.suppressUnsupportedBlurAnimations()' in depth
assert 'springAnimation.animateToFinalPosition(newRadius.toFloat())' in depth
print("G1513_SOURCE_OVERLAY_DEFAULT_OFF_DEVICE_ON_NOOP_ZERO_SPRING_GUARD_PASS")
