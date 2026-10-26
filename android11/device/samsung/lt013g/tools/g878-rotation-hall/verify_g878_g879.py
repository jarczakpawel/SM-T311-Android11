#!/usr/bin/env python3
"""SM-T311 Android11 rectangular geometry + native lid-policy source gate.

No ADB, no device changes, no screenshot/video I/O, no media playback.
"""
import os
from pathlib import Path
from xml.etree import ElementTree as ET

ANDROID=Path(__file__).resolve().parents[5]

def read(rel):return (ANDROID/rel).read_text()

ui=read('frameworks/base/packages/SystemUI/src/com/android/systemui/screenrecord/ScreenMediaRecorder.java')
service=read('frameworks/base/packages/SystemUI/src/com/android/systemui/screenrecord/RecordingService.java')
logic=read('frameworks/base/services/core/java/com/android/server/display/LogicalDisplay.java')
wm=read('frameworks/base/services/core/java/com/android/server/policy/PhoneWindowManager.java')
defaults=read('frameworks/base/packages/SettingsProvider/src/com/android/providers/settings/DatabaseHelper.java')
media=ET.fromstring(read('device/samsung/lt013g/overlay/frameworks/base/packages/SystemUI/res/values/t311_platform_v107.xml'))
vals={n.attrib.get('name'):n.text.strip() for n in media}
assert vals['config_screenRecorderMaxDimension']=='1024'
assert vals['config_screenRecorderMaxFramerate']=='30'
# G885: The 800x800 square proposal G878 was REJECTED; its code was
# reverted from frameworks/base in Git, not silently dropped from history.
# No square/letterbox canvas is allowed in the product source tree.
assert 'screenHeight = screenWidth;' not in ui
assert 'squareSide' not in ui
assert 'square encoder surface' not in service
assert 'fixed AVC capture surface=' not in ui
assert 'maxRecordDimension & ~15' in ui
assert 'mMediaRecorder.setVideoSize(screenWidth, screenHeight)' in ui
assert 'mMediaProjection.createVirtualDisplay(' in ui
assert 'mRecordingWidth,' in ui and 'mRecordingHeight,' in ui
# G908: user video proves rotation/resizing in a constant 640x1024 AVC
# track is unwanted. Freeze CURRENT orientation during capture, then restore
# original auto/manual policy, not blindly thawing a manually locked tablet.
assert 'if (!"lt013g".equals(Build.DEVICE) && !auto)' in service
assert 'wm.freezeRotation(orientation);' in service
assert 'wm.freezeRotation(mOriginalRotation);' in service
assert 'G908_RECORDING_ROTATION_RESTORED' in service
assert 'displayRectWidth = physWidth;' in logic and 'displayRectHeight = physHeight;' in logic
assert 'displayRectTop = (physHeight - displayRectHeight) / 2' in logic
assert 'displayRectLeft = (physWidth - displayRectWidth) / 2' in logic
# The real tablet panel is 800x1280. The validated rectangle is
# 640x1024 in portrait or 1024x640 in landscape (cap 1024).
max_dim=int(vals['config_screenRecorderMaxDimension'])
assert max_dim == 1024
for width,height in ((800,1280),(1280,800)):
    longest=max(width,height)
    out_w=(width*max_dim//longest)&~15
    out_h=(height*max_dim//longest)&~15
    assert (out_w,out_h) in ((640,1024),(1024,640))
    assert out_w!=out_h and out_w*height==out_h*width
    assert out_w*out_h==655360
print('G885_RECTANGULAR_640X1024_1024X640_NATIVE_ASPECT_SOURCE_ONLY_PASS')
print('G885_DYNAMIC_MID_STREAM_ROTATION_NOT_IMPLEMENTED_OR_CLAIMED')

cfg=ET.fromstring(read('device/samsung/lt013g/overlay/frameworks/base/core/res/res/values/config.xml'))
found={n.get('name'):(n.text or '').strip() for n in cfg}
assert found.get('config_lidControlsSleep')=='true'
assert 'Settings.Global.LID_BEHAVIOR' in wm and 'case LID_BEHAVIOR_SLEEP' in wm
assert 'Global.LID_BEHAVIOR, defaultLidBehavior' in defaults
assert 'R.bool.config_lidControlsSleep' in defaults
# No extra daemon, no kernel generic hooks: reuse already functional GPX2_4.
kernel=Path(os.environ.get('SMT311_KERNEL_ROOT',
        str(ANDROID/'kernel/samsung/lt013g')))
dts=kernel/'arch/arm/boot/dts/samsung/exynos4212-tab3.dtsi'
if dts.is_file():
    s=dts.read_text();piece=s.split('switch-hall-sensor {',1)[1].split('};',1)[0]
    assert 'gpios = <&gpx2 4 GPIO_ACTIVE_LOW>;' in piece
    assert 'linux,code = <SW_LID>;' in piece
    assert 'wakeup-source;' in piece
else:
    print('G879_KERNEL_SOURCE_NOT_PRESENT_CHECK_ANDROID_HALL_OVERLAY_ONLY')
print('G879_GPIO_HALL_SW_LID_ANDROID_NATIVE_SLEEP_WAKE_POLICY_SOURCE_PASS')
