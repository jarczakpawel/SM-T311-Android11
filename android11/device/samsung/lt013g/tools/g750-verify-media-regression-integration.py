#!/usr/bin/env python3
"""Fail-closed SM-T311 media integration checks before full clean ROM builds.

Source-level gates supplement, not replace, on-device camera and video QA.
"""
from pathlib import Path
import sys
import xml.etree.ElementTree as ET

A = Path(__file__).resolve().parents[4]
errors = []
passed = []

def check(title, ok):
    (passed if ok else errors).append(title)
    print(("PASS " if ok else "FAIL ") + title)

def read(path):
    f = A / path
    check("source present: " + path, f.is_file())
    return f.read_text() if f.is_file() else ""

board = read("device/samsung/lt01-common/BoardConfigCommon.mk")
check("vendor SELinux directory integrated", "BOARD_VENDOR_SEPOLICY_DIRS += device/samsung/lt01-common/sepolicy/vendor" in board)
check("private SELinux directory integrated", "BOARD_PLAT_PRIVATE_SEPOLICY_DIR += device/samsung/lt01-common/sepolicy/private" in board)

hal = read("hardware/interfaces/camera/device/3.4/default/ExternalCameraDevice.cpp")
check("front camera truthful 25fps present",
      "add(640,480,25)" in hal and 'const int32_t fpsRanges[] = {15, 25}' in hal)
framework = read("frameworks/av/services/camera/libcameraservice/api1/client2/Parameters.cpp")
check("camera1 25fps guard limited to this model and front lens",
      'strcmp(productDevice, "lt013g") == 0' in framework
      and 'cameraFacing == CAMERA_FACING_FRONT' in framework
      and 'kT311Front25FpsFrameDurationNs = 40000000LL' in framework
      and 'minFrameDuration > kT311Front25FpsFrameDurationNs' in framework)

gpu = read("device/samsung/lt01-common/sepolicy/vendor/lt013g_drm_access.te")
check("screen recording mediaserver DRM search", "allow mediaserver gpu_device:dir search;" in gpu)
fd = read("device/samsung/lt01-common/sepolicy/private/lt013g_mediaserver_frame_fd.te")
check("MediaStore thumbnail fd receiver", "allow { appdomain -isolated_app } mediaserver:fd use;" in fd)
check("MediaStore thumbnail memory mapping", 
      "allow { appdomain -isolated_app } mediaserver_tmpfs:file { getattr read write map };" in fd)
interop = read("device/samsung/lt01-common/sepolicy/vendor/lt013g_media_memfd_interop.te")
check("hardware AVC decoder receives mediaserver bufferpool FMQ",
      "allow mediacodec mediaserver_tmpfs:file { getattr read write map };" in interop)

prod = read("device/samsung/lt01-common/common.mk")
check("device media codec XML copied into ROM",
      "media-v183/media_codecs_c2.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_c2.xml" in prod)
xml = read("device/samsung/lt01-common/media-v183/media_codecs_c2.xml")
try:
    root = ET.fromstring(xml)
    names = {e.attrib.get("name") for e in root.findall(".//MediaCodec")}
except ET.ParseError:
    names = set()
check("hardware AVC video encoder registered", "c2.v4l2.avc.encoder" in names)
check("hardware AVC video decoder registered", "c2.v4l2.avc.decoder" in names)
vda = read("external/v4l2_codec2/plugin_store/C2VdaBqBlockPool.cpp")
check("VDA private output pool type is known and still matches the HIDL bridge",
      "kTypeVdaBufferQueue = TYPE_BUFFERQUEUE + 256" in vda)
decoder = read("external/v4l2_codec2/components/V4L2DecodeComponent.cpp")
check("VDA marks output slot as shared before sending via Codec2 HIDL",
      "MarkBlockPoolDataAsShared(constBlock);" in decoder)
hidl = read("frameworks/av/media/codec2/hidl/1.0/utils/types.cpp")
check("Codec2 HIDL native-serializes VDA private BufferQueue output",
      "_C2BlockPoolData::TYPE_BUFFERQUEUE + 256" in hidl
      and "return _addBaseBlock(" in hidl
      and "Unknown C2BlockPoolData type." in hidl)


ui = read("frameworks/base/packages/SystemUI/src/com/android/systemui/screenrecord/ScreenMediaRecorder.java")
check("SystemUI normal rectangular recording (no square canvas)",
      "screenWidth = canvasSize;" not in ui
      and "screenHeight = canvasSize;" not in ui
      and "final int longestDimension = Math.max(screenWidth, screenHeight);" in ui)
check("SystemUI uses MediaProvider bounded thumbnail",
      "getContentResolver().loadThumbnail(" in ui)
check("SystemUI even-aligned rectangular capture preserving native aspect",
      "maxRecordDimension & ~1" in ui and "longestDimension) & ~1L" in ui)
check("G757 recorder starts hardware encoder before connecting virtual display",
      ui.index("mMediaRecorder.start();") < ui.index(
          "mMediaProjection.createVirtualDisplay("))
check("G757 mirrored virtual display uses native aspect and hardware input surface",
      "mRecordingWidth = screenWidth;" in ui
      and "mRecordingHeight = screenHeight;" in ui
      and "mRecordingDensityDpi = metrics.densityDpi;" in ui
      and "DisplayManager.VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR" in ui)
check("G759 rejects G758 final-surface detach, which overstretches idle MP4",
      "mVirtualDisplay.setSurface(null);" not in ui
      and "SystemClock.sleep(90);" not in ui)


check("SystemUI higher VBR quality without 12M hardware cap overflow",
      "VIDEO_FRAME_RATE_TO_RESOLUTION_RATIO = 8" in ui)
rotation = read("frameworks/base/packages/SystemUI/src/com/android/systemui/screenrecord/RecordingService.java")
check("screen recorder locks currently selected orientation, not natural-only",
      "wm.freezeRotation(orientation);" in rotation
      and "getDefaultDisplay().getRotation()" in rotation)
check("screen recorder restores original autorotation on stop/start error/destroy",
      "wm.freezeRotation(orientation);" in rotation
      and "getWindowManagerService().thawRotation()" in rotation
      and "finally {\n            restoreAutoRotation();" in rotation
      and "public void onDestroy()" in rotation)

device_overlay = A / "device/samsung/lt013g/overlay/frameworks/base/packages/SystemUI/res/values/t311_platform_v107.xml"
try:
    config = {el.attrib.get("name"): (el.text or "").strip() for el in ET.parse(device_overlay).getroot()}
except (ET.ParseError, OSError):
    config = {}
check("G820: screen recorder 1024 longer edge physically decoder/Gallery-compatible",
      config.get("config_screenRecorderMaxDimension") == "1024")
check("screen recorder frame rate cap", config.get("config_screenRecorderMaxFramerate") == "30")
device = read("device/samsung/lt013g/t311-platform.mk")
check("SystemUI overlay included in ROM", "DEVICE_PACKAGE_OVERLAYS += device/samsung/lt013g/overlay" in device)
media_provider = read("packages/providers/MediaProvider/src/com/android/providers/media/MediaProvider.java")
check("G753 quota ioctl runs only when project quota supported on SM-T311",
      '\"lt013g\".equals(Build.DEVICE)' in media_provider
      and 'SystemProperties.getBoolean(\"external_storage.projid.enabled\", false)' in media_provider)
wifi_attr = read("system/connectivity/wificond/net/nl80211_attribute.cpp")
check("G754 wificond masks kernel NLA_NESTED flag in attribute lookup",
      "(header->nla_type & NLA_TYPE_MASK) == attr_id" in wifi_attr)
check("G754 wificond masks Netlink type flags from GetAttributeId",
      "return header->nla_type & NLA_TYPE_MASK;" in wifi_attr)
check("G754 rejects invalid or zero-length Netlink attributes",
      "header->nla_len < NLA_HDRLEN" in wifi_attr)
wifi_test = read("system/connectivity/wificond/tests/nl80211_attribute_unittest.cpp")
check("G754 regression tests nested station info and malformed zero length",
      "NestedFlagIsNotPartOfAttributeId" in wifi_test
      and "RejectZeroLengthNestedAttribute" in wifi_test)
gnss_service = read("hardware/interfaces/gnss/1.0/default/service.cpp")
check("G755 GNSS service expects vendor BinderFS vndbinder driver",
      'ProcessState::initWithDriver("/dev/vndbinder")' in gnss_service)
gnss_policy = read("device/samsung/lt01-common/sepolicy/vendor/lt013g_gnss_network.te")
check("G755 narrow enforcing SELinux allow for GNSS vendor Binder",
      "allow hal_gnss_default vndbinder_device:chr_file { getattr open read write ioctl map };" in gnss_policy)




print(f"INTEGRATION: {len(passed)} PASS / {len(errors)} FAIL")
if errors:
    sys.exit(1)
