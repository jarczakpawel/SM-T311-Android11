# SM-T311 Camera Port State

Updated: 2026-09-15T00:53:31,505551163+02:00

## Architecture

- LineageOS 18.1 / Android 11
- Linux 6.12.98
- Native HAL3/HIDL provider external/0
- Rear: ISX012 -> CSIS0 -> FIMC0 -> /dev/video1
- Front: SR130PC20 -> CSIS1 -> FIMC1 -> /dev/video3
- Raw baseline: 640x480 UYVY, VIDEO_CAPTURE_MPLANE

## Persisted camera fixes

- v210: modern V4L2 subdev state/finalization
- v211: CSIS subdev state
- v212: FIMC UYVY / CSI-2 YCbCr422 0x1e
- v214: dual graph + native Android 11 HAL3
- HOTFIX5: ISX012 identity/OTP/OM/CM stock-derived bring-up
- HOTFIX6/7: direct provider + complete CameraDevice runtime closure
- HOTFIX8: persistent video-node permissions; CameraService enumerates 2 cameras
- HOTFIX9: rear power only at STREAMON + cold STREAMOFF
- HOTFIX10: per-sensor MCLK active/idle pinctrl
- HOTFIX11: front cold lifecycle + 150ms stock stream-off delay + FIMC failed-start rollback
- HOTFIX12: removed erroneous mshc_0 -> LDO22 mapping; LDO22 is dedicated 5M_AF_2.8V; AF now reaches real off state
- HOTFIX13: camera I2C0 restored from 400 kHz to stock T311 100 kHz. Stock mach-tab3 used s3c_i2c0_set_platdata(NULL), whose driver default is standard-mode 100 kHz.

## HOTFIX13 result

PASS: HOTFIX13 restored camera I2C0 to stock 100 kHz and all 41 raw stress invocations passed, including all 12 rear-after-front switches.

## Build integration

Current kernel is synchronized into Android forced prebuilt:

`/home/debian/android/lineage-18.1-sm-t311/device/samsung/lt01-common/prebuilt/pjarczak/kernel`

Full build helper:

`/home/debian/android/lineage-18.1-sm-t311/device/samsung/lt013g/BUILD-PJARCZAK-CAMERA.sh`

This source-tree file is the persistent handoff.

## HOTFIX14 Camera2 framework validation - 2026-09-15T01:05:24,851190830+02:00

FAIL_COLLECTED: Camera2 end-to-end test ran, but one or more framework/HAL/session/frame gates failed. See HOTFIX14B result archive.

Persistent test source:
`device/samsung/lt013g/tools/t311-camera2-smoke`

Validation path:
Camera2 -> CameraService -> HIDL HAL3 -> provider external/0 -> FIMC -> sensor.

### HOTFIX14C Camera2 foreground lifecycle - 2026-09-15T01:11:20,260711820+02:00

FAIL_COLLECTED: foreground lifecycle restriction is fixed in the persistent tester, but Camera2/HAL3 hit the next failure. Inspect HOTFIX14C archive.

Tester starts camera work only from a resumed foreground Activity. Kernel/HAL/provider were not changed by HOTFIX14C.

### HOTFIX15 fixed-FPS V4L2 fallback / Camera2 - 2026-09-15T01:23:19,638739581+02:00

FAIL_COLLECTED: FIMC unsupported FPS ioctl is now non-fatal in persistent HAL source; Camera2 reached the next failure. Inspect HOTFIX15 archive.

Persistent HAL source:
`hardware/interfaces/camera/device/3.4/default/ExternalCameraDeviceSession.cpp`

FIMC capture does not implement VIDIOC_G_PARM/VIDIOC_S_PARM. HOTFIX15 treats
EINVAL/ENOTTY/ENOSYS/EOPNOTSUPP and missing V4L2_CAP_TIMEPERFRAME as a fixed-fps
device instead of failing configureStreams(). The advertised SM-T311 safe mode
remains 640x480 @ 30 fps.

Kernel was not changed by HOTFIX15.

### HOTFIX16 YUV_420_888 -> YV12 gralloc compatibility - 2026-09-15T01:31:50,502072856+02:00

FAIL_COLLECTED: HOTFIX16 persists the YUV_420_888 -> YV12 HAL override, but Camera2 reached the next failure. Inspect HOTFIX16 gralloc/HAL logs.

Persistent HAL source:
`hardware/interfaces/camera/device/3.4/default/ExternalCameraDeviceSession.cpp`

Reason:
SM-T311 minigbm/gralloc0 rejects flexible HAL_PIXEL_FORMAT_YCbCr_420_888
(format 35) with camera-write + CPU read/write usage. HOTFIX16 uses the Camera3
HalStream override mechanism and requests concrete YV12 buffers for an app
YUV_420_888 stream. Android ImageReader explicitly accepts YV12 as a compatible
producer format for YUV_420_888.

Kernel was not changed by HOTFIX16.

### HOTFIX17C active gralloc module - 2026-09-15T01:57:09,364672409+02:00

FAIL_COLLECTED: exact active gralloc module (gralloc.minigbm_exynos) was rebuilt and installed from HOTFIX17 sources; Camera2 reached the next failure. Inspect HOTFIX17C logs.

HOTFIX17 minigbm source fix remains persistent:
`external/minigbm/cros_gralloc/cros_gralloc_helpers.cc`

HOTFIX17C builds and installs the exact gralloc module selected by:
`ro.hardware.gralloc=minigbm_exynos`

Active runtime:
`/system/vendor/lib/hw/gralloc.minigbm_exynos.so`

Kernel/BOOT were not changed.

### HOTFIX21 Exynos NV12 single-GEM CPU Camera2 backing - 2026-09-15T12:26:13,607053481+02:00

HOTFIX20B restored the graphics-stable V124 RGBA alignment and V127 contiguous
Exynos GEM allocation. Camera2 then reached stream allocation without global
Mesa/DRI/system_server regressions, but cros_gralloc rejected NV12 because the
Exynos backend represented its Y and UV planes as two separate kernel GEM
buffers. cros_gralloc requires one kernel buffer per BO.

HOTFIX21 keeps two logical NV12 planes but, only for CPU-only NV12 allocations,
places Y and UV in one contiguous Exynos GEM. Plane 0 starts at offset 0 and
plane 1 starts at the end of the padded Y plane. Hardware NV12 paths keep the
existing per-plane allocation path. HOTFIX17 and HOTFIX19 remain unchanged.

Kernel/BOOT are unchanged by HOTFIX21.

HOTFIX21 validation - 2026-09-15T12:29:10,436459607+02:00: PASS
- global graphics safety: PASS
- cros_gralloc one-kernel-buffer rejection: 0
- GraphicBufferAllocator 640x480 YUV allocation failures: 0
- Camera2 13-session rear/front regression: PASS

### HOTFIX22 Camera2 capability audit tester - 2026-09-15T12:41:02,187989131+02:00

Persistent tester now validates every advertised YUV_420_888, JPEG and PRIVATE
output size for both cameras, realistic PRIVATE-preview + JPEG-still sessions,
MediaRecorder H.264 recording, and rear/front switching. HOTFIX22 changes only
the test application; kernel, camera HAL and gralloc are not modified.

### HOTFIX23 JPEG/PRIVATE CPU output backing - 2026-09-15T13:03:24,379707277+02:00

Validated design changes:
- External camera HAL output buffers are CPU-produced. CAMERA_OUTPUT is removed
  from producerUsage for every output type while preserving consumer/requested
  usage such as CAMERA_INPUT, TEXTURE or VIDEO_ENCODER.
- Exynos minigbm now advertises R8 for Camera3 BLOB/JPEG buffers.
- Exynos minigbm now advertises Android YV12 (DRM_FORMAT_YVU420_ANDROID) and
  allocates all three Y/V/U logical planes from one physically contiguous GEM
  with Android-required YV12 strides and plane offsets.
- HOTFIX17, HOTFIX19, V124, V127 and HOTFIX21 remain in force.
- Video encoder creation is intentionally not changed here; H22 showed
  StagefrightRecorder failing before Camera2 session creation and the current
  media codec registry contains no video encoder entries.

Kernel/BOOT are unchanged by HOTFIX23.

### HOTFIX24 Samsung AVC OMX encoder registry - 2026-09-15T13:16:45,840069980+02:00

The Android 11 media codec registry now exposes the already packaged Samsung
hardware AVC encoder as OMX.SEC.AVC.Encoder. The legacy SM-T311 Android 7
configuration used this component for H.264 recording, including 640x480@30 on
both cameras and 1280x720@30 on the rear camera.

Persistent source changes:
- device/samsung/lt01-common/media-v183/media_codecs.xml
- device/samsung/lt01-common/media-v183/media_codecs_samsung_video.xml
- device/samsung/lt01-common/common.mk
- device/samsung/lt013g/tools/t311-camera2-smoke/MainActivity.java

HOTFIX24 does not replace the hardware encoder with a software codec and does
not change kernel, camera HAL, minigbm or BOOT. If OMX component creation fails,
the collected logs are the next-layer MFC/ABI/allocator diagnostic.

### HOTFIX24B Samsung AVC OMX runtime payload - 2026-09-15T13:28:30,547162354+02:00

HOTFIX24 stopped before runtime installation because the running image did not
contain libOMX.SEC.AVC.Encoder.so. HOTFIX24B builds the declared Samsung OMX/MFC
modules explicitly and installs their product artifacts instead of assuming that
PRODUCT_PACKAGES are already present in the flashed image.

The legacy Samsung SEC OMX core also requires /system/etc/secomxregistry to
load component libraries. HOTFIX24B persists that registry with
libOMX.SEC.AVC.Encoder.so and keeps the Android 11 media_codecs registration.

No kernel, camera HAL, minigbm or BOOT change is made by HOTFIX24B.

### HOTFIX24C isolated Exynos4 multimedia module discovery

The SM-T311 device Android.mk now parses only hardware/samsung/exynos/multimedia
with TARGET_SOC temporarily set to exynos4x12. The previous TARGET_SOC value is
restored immediately afterwards, so hardware/samsung/Android.mk cannot pull the
legacy Exynos4 graphics/HWC/NFC stack into the Android 11 Lima/minigbm build.

This makes the SEC OMX/MFC modules real build-graph targets while preserving the
modern graphics stack. No kernel, camera HAL, minigbm or BOOT change is made.

### HOTFIX24D isolated Exynos4 multimedia module discovery

The SM-T311 device Android.mk now parses only hardware/samsung/exynos/multimedia
with TARGET_SOC temporarily set to exynos4x12. The previous TARGET_SOC value is
restored immediately afterwards, so hardware/samsung/Android.mk cannot pull the
legacy Exynos4 graphics/HWC/NFC stack into the Android 11 Lima/minigbm build.

This makes the SEC OMX/MFC modules real build-graph targets while preserving the
modern graphics stack. No kernel, camera HAL, minigbm or BOOT change is made.

### HOTFIX24E minimal SEC AVC build graph

The SM-T311 device Android.mk parses only the Samsung Exynos4 multimedia
modules required by the AVC encoder: libfimc/libhwconverter support, MFC API,
CSC, SEC OMX OSAL/core/common/video-encoder base, H.264 encoder and the legacy
Stagefright OMX plugin. TARGET_SOC/TARGET_OMX_PATH/TARGET_HAL_PATH are set only
inside this isolated block and restored immediately afterwards.

This avoids pulling the complete Android 7 Exynos multimedia decoder stack or
legacy Exynos4 gralloc/HWC/NFC into the Android 11 Lima/minigbm build.

### HOTFIX24E Android 11 SEC AVC header-port compatibility

Removed obsolete LOCAL_COPY_HEADERS use from the selected Exynos4 MFC and CSC
modules. The SEC video encoder base obtains CSC headers directly from source,
and the H.264 encoder obtains both MFC and CSC headers directly from source.
The full legacy multimedia tree is not parsed.
