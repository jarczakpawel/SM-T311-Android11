#!/usr/bin/env python3
"""G900: keep real SM-T311 hardware and Android11 constraints in source.

Read-only. Uses ONLY the project sources (plus optional previously built
SystemUI and services.jar profile); no ADB, kernels flashed or system writes.
The goal is to catch accidental loss of hardware-specific decisions before
an operator manually publishes a full kernel/Android tree.
"""
from pathlib import Path
import os
import re
import xml.etree.ElementTree as ET

ANDROID = Path(__file__).resolve().parents[5]
KERNEL = Path(os.environ.get('SMT311_KERNEL_ROOT', str(ANDROID / 'kernel/samsung/lt013g')))

def read(relative):
    return (ANDROID / relative).read_text()

def checked(name, success):
    assert success, f'G900_BLOCKED_{name}'
    print(f'G900_{name}_SOURCE_PASS')

board = read('device/samsung/lt01-common/BoardConfigCommon.mk')
checked('32_BIT_CORTEX_A9_EXYNOS', all(marker in board for marker in (
    'TARGET_ARCH := arm', 'TARGET_ARCH_VARIANT := armv7-a-neon',
    'TARGET_CPU_VARIANT := cortex-a9', 'TARGET_SUPPORTS_64_BIT_APPS := false',
    'TARGET_BOARD_PLATFORM := exynos4')))
checked('MALI400_MESA_LIMA_KMSRO',
    'BOARD_GPU_DRIVERS := lima kmsro' in board and
    'BOARD_DRM_HWCOMPOSER_BUFFER_IMPORTER := minigbm' in board)

common = read('device/samsung/lt01-common/common.mk')
checked('MESA2625_EXACT_SIX_STAGED_LIBRARIES',
    'prebuilt/pjarczak/mesa26-2625' in common and
    'G867_SIX_STAGED_ARM32_SHA256_PASS' in common and
    all(name in common for name in ('libEGL_mesa.so', 'libGLESv1_CM_mesa.so',
        'libGLESv2_mesa.so', 'libgallium_dri.so',
        'libgbm_mesa', 'libdrm_mesa26')))

layout = read('device/samsung/lt013g/overlay/frameworks/base/packages/SystemUI/res/values/t311_platform_v107.xml')
resources = {x.get('name'): (x.text or '').strip() for x in ET.fromstring(layout)}
checked('RECORDER_LONG_EDGE_1024_AND_30FPS',
    resources.get('config_screenRecorderMaxDimension') == '1024' and
    resources.get('config_screenRecorderMaxFramerate') == '30')
ui = read('frameworks/base/packages/SystemUI/src/com/android/systemui/screenrecord/ScreenMediaRecorder.java')
checked('RECORDER_16_PIXEL_ALIGNED_RECTANGLES',
    'maxRecordDimension & ~15' in ui and 'longestDimension' in ui and
    'mMediaRecorder.setVideoSize(screenWidth, screenHeight)' in ui and
    'mMediaRecorder.setVideoEncoder(MediaRecorder.VideoEncoder.H264)' in ui and
    'screenHeight = screenWidth;' not in ui)
for w,h,out_w,out_h in [(800,1280,640,1024),(1280,800,1024,640)]:
    scale = int(resources['config_screenRecorderMaxDimension']) & ~15
    actual_w = max(16,((w*scale)//max(w,h)) & ~15)
    actual_h = max(16,((h*scale)//max(w,h)) & ~15)
    checked(f'ENCODER_GEOMETRY_{out_w}X{out_h}',
        (actual_w,actual_h) == (out_w,out_h) and
        actual_w*actual_h <= 655360 and
        actual_w*h == actual_h*w and
        actual_w % 16 == actual_h % 16 == 0)
checked('RECORDER_ATTACH_AFTER_MEDIARECORDER_START',
    ui.index('mMediaRecorder.start();') < ui.index('mMediaProjection.createVirtualDisplay('))
service = read('frameworks/base/packages/SystemUI/src/com/android/systemui/screenrecord/RecordingService.java')
start = service.split('private void lockCurrentRotationForRecording() throws RemoteException {',1)[1]
checked('TABLET_RECORDER_FIXED_ORIENTATION_AND_POLICY_RESTORATION',
    'if (!"lt013g".equals(Build.DEVICE) && !auto)' in start and
    'wm.freezeRotation(orientation);' in start and
    'mOriginalAutoRotation = auto;' in start and
    'mOriginalRotation = orientation;' in start and
    'G908_RECORDING_ROTATION_FIXED=' in start and
    'wm.thawRotation();' in service and
    'wm.freezeRotation(mOriginalRotation);' in service and
    'G908_RECORDING_ROTATION_RESTORED' in service)
logical = read('frameworks/base/services/core/java/com/android/server/display/LogicalDisplay.java')
checked('ANDROID11_ASPECT_PRESERVING_MIRROR',
    'physWidth * displayInfo.logicalHeight' in logical and
    'physHeight * displayInfo.logicalWidth' in logical and
    'displayRectLeft = (physWidth - displayRectWidth) / 2' in logical and
    'displayRectTop = (physHeight - displayRectHeight) / 2' in logical)

encoder = read('external/v4l2_codec2/components/V4L2EncodeComponent.cpp')
checked('MFC_GOP30_BOTH_RECTANGULAR_ONLY',
    all(item in encoder for item in (
        'strcmp(device, "lt013g") == 0',
        'mVisibleSize.width() == 640 && mVisibleSize.height() == 1024',
        'mVisibleSize.width() == 1024 && mVisibleSize.height() == 640',
        'V4L2_CID_MPEG_VIDEO_GOP_SIZE, gopSize',
        'std::min(mKeyFramePeriod, 30u)',
        'V4L2_CID_MPEG_VIDEO_GOP_SIZE, 0')) and
    'isT311SquareRecorder' not in encoder)

# G901: AOSP Android11 lmkd PSI strategy must actually have kernel PSI,
# memory cgroups and an appropriate userspace property. ZRAM is independent
# and may be completely unused when the tablet is not under RAM pressure.
props = read('device/samsung/lt01-common/system.prop')
prop = dict(re.findall(r'^([a-zA-Z0-9_.-]+)=(.+)$', props, flags=re.M))
checked('ANDROID11_LMKD_PSI_NEW_STRATEGY_ENABLED',
    prop.get('ro.lmk.use_psi') == 'true' and
    prop.get('ro.lmk.use_new_strategy') == 'true' and
    prop.get('ro.lmk.use_minfree_levels') == 'false')

perf = read('device/samsung/lt01-common/lt01-performance.mk')
checked('SYSTEM_SERVER_FULL_AOT_SPEED_EXPLICIT',
    'PRODUCT_SYSTEM_SERVER_COMPILER_FILTER := speed' in perf and
    'PRODUCT_DEX_PREOPT_BOOT_IMAGE_PROFILE_LOCATION' in perf and
    re.search(r'^\s*ro\.config\.low_ram\s*[:?+]?=\s*true\s*$', perf, re.M) is None)
profile = ANDROID/'out/target/product/lt013g/system/framework/services.jar.prof'
if profile.is_file():
    checked('BUILT_SYSTEM_SERVER_HAS_NONEMPTY_PROFILE',profile.stat().st_size>1000)
else:
    print('G900_PROFILE_BUILD_OUTPUT_ABSENT_NO_RUNTIME_CLAIM')

config = KERNEL/'arch/arm/configs/sm_t311_android11_defconfig'
reserved = KERNEL/'arch/arm/boot/dts/samsung/exynos4212-tab3-reserved-memory.dtsi'
if config.is_file() and reserved.is_file():
    conf = config.read_text()
    checked('ANDROID11_KERNEL_PSI_MEMCG_AND_ZRAM',
        all(re.search(r'^' + re.escape(entry) + r'$', conf, re.M)
            for entry in ('CONFIG_PSI=y', 'CONFIG_MEMCG=y',
                          'CONFIG_ZRAM=y', 'CONFIG_ZSMALLOC=y')))
    checked('KERNEL_CMA_128MB_NO_UNPROVEN_INCREASE',
        re.search(r'^CONFIG_CMA_SIZE_MBYTES=128$',config.read_text(),re.M) is not None)
    section = reserved.read_text()
    checked('S5P_MFC_36_PLUS_8MB_IS_SEPARATE_NOMAP',
        'size = <0x2400000>;' in section and
        'size = <0x800000>;' in section and
        section.count('no-map;') >= 2)
else:
    print('G900_KERNEL_SOURCE_NOT_AVAILABLE_ANDROID_SOURCE_ONLY_CHECKS_PASS')
print('G900_SM_T311_EXACT_HARDWARE_AND_ANDROID11_RELEASE_GUARD_PASS')
