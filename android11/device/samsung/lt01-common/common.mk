LOCAL_PATH := device/samsung/lt01-common

# SM-T311 shipped with Android 4.2.2 (Jelly Bean MR2).
$(call inherit-product, vendor/lineage/build/target/product/product_launched_with_j_mr2.mk)

PRODUCT_CHARACTERISTICS := tablet
PRODUCT_AAPT_CONFIG := normal large hdpi
PRODUCT_AAPT_PREF_CONFIG := hdpi

# Keep v48 dependency-free. Permissions, init, HALs, firmware, modules,
# graphics, audio, radio, sensors and media are introduced in audited stages.

# Selective SM-T311 Android 11 performance baseline.
$(call inherit-product, device/samsung/lt01-common/lt01-performance.mk)

# BEGIN SM-T311 V56 ROOT RAMDISK FILES
PRODUCT_COPY_FILES += \
    device/samsung/lt01-common/rootdir/fstab.lt013g:ramdisk/fstab.lt013g \
    device/samsung/lt01-common/rootdir/init.lt013g.rc:ramdisk/init.lt013g.rc \
    device/samsung/lt01-common/rootdir/ueventd.lt013g.rc:ramdisk/ueventd.lt013g.rc
# END SM-T311 V56 ROOT RAMDISK FILES

# BEGIN SM-T311 V59 FIRST BOOT GRAPHICS
PRODUCT_SOONG_NAMESPACES += external/mesa3d

PRODUCT_PACKAGES += \
    gralloc.minigbm_exynos \
    hwcomposer.drm_minigbm \
    android.hardware.graphics.mapper@2.0-impl-2.1 \
    android.hardware.graphics.allocator@2.0-impl \
    android.hardware.graphics.allocator@2.0-service \
    android.hardware.graphics.composer@2.1-service
# END SM-T311 V59 FIRST BOOT GRAPHICS

# SM-T311 ONLY: the physical Mali-400 release is Mesa 26.2.5.
# No fallback to obsolete 26.2.4 or libGLES_mesa, even when command-line
# flags are overridden. Six private, SHA-pinned ARM32 outputs are mandatory.
ifneq ($(SMT311_USE_MESA26),true)
$(error SM-T311 requires Mesa 26.2.5: SMT311_USE_MESA26 must be true)
endif
ifneq ($(SMT311_MESA26_VERSION),26.2.5)
$(error SM-T311 requires Mesa 26.2.5: refuse missing/old version $(SMT311_MESA26_VERSION))
endif
SMT311_MESA26_INPUT_DIR := device/samsung/lt01-common/prebuilt/pjarczak/mesa26-2625
SMT311_MESA26_STAGE_RESULT := $(shell /bin/bash device/samsung/lt01-common/mesa26-g816/stage.sh --verify-staged 2>&1)
ifneq ($(SMT311_MESA26_STAGE_RESULT),G867_SIX_STAGED_ARM32_SHA256_PASS)
$(error SM-T311 Mesa 26.2.5 staged ARM32 outputs invalid: $(SMT311_MESA26_STAGE_RESULT))
endif
# GBM + DRM26 are checked, unstripped prebuilt modules with explicit ELF deps.
# Product copy installs only the remaining four ARM32 libraries.
SMT311_MESA_GALLIUM_INPUT := $(SMT311_MESA26_INPUT_DIR)/libgallium_dri.so
ifeq ($(SMT311_MESA_GLPB_OPTIN),true)
ifneq ($(TARGET_PRODUCT),lineage_lt013g)
$(error G1752 Mesa GLPB is restricted to lt013g)
endif
SMT311_MESA_GALLIUM_INPUT := device/samsung/lt01-common/mesa26-g816/experimental-g1749/libgallium_dri.so
SMT311_MESA_GALLIUM_SHA := $(shell /usr/bin/sha256sum $(SMT311_MESA_GALLIUM_INPUT) 2>/dev/null | /usr/bin/cut -d' ' -f1)
ifneq ($(SMT311_MESA_GALLIUM_SHA),fe8a15d701915f0178114d095555a2eb4d54dae95d5bbabd6f61a4d80639a3e0)
$(error G1752 Mesa GLPB SHA mismatch; refuse release)
endif
endif
PRODUCT_PACKAGES += libgbm_mesa libdrm_mesa26
PRODUCT_COPY_FILES += \
    $(SMT311_MESA26_INPUT_DIR)/libEGL_mesa.so:$(TARGET_COPY_OUT_VENDOR)/lib/egl/libEGL_mesa.so \
    $(SMT311_MESA26_INPUT_DIR)/libGLESv1_CM_mesa.so:$(TARGET_COPY_OUT_VENDOR)/lib/egl/libGLESv1_CM_mesa.so \
    $(SMT311_MESA26_INPUT_DIR)/libGLESv2_mesa.so:$(TARGET_COPY_OUT_VENDOR)/lib/egl/libGLESv2_mesa.so \
    $(SMT311_MESA_GALLIUM_INPUT):$(TARGET_COPY_OUT_VENDOR)/lib/libgallium_dri.so
# END SM-T311 Mesa26 release lock
# SM-T311 V151 - Android Light HAL
PRODUCT_PACKAGES += \
    android.hardware.light@2.0-service.lt013g

PRODUCT_COPY_FILES += \
    device/samsung/lt01-common/rootdir/init.lt013g.lights.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/init.lt013g.lights.rc

# BEGIN SM-T311 V158 REBOOT + IIO SENSORS + AUTO BRIGHTNESS
PRODUCT_PACKAGES += \
    lt013g-reboot-guard \
    sensors.lt013g \
    android.hardware.sensors@1.0-impl \
    android.hardware.sensors@1.0-service \
    SMT311FrameworkOverlay
# END SM-T311 V158 REBOOT + IIO SENSORS + AUTO BRIGHTNESS

# BEGIN SM-T311 V161 COMMON
PRODUCT_PACKAGES += \
    android.hardware.light@2.0-service.lt013g \
    android.hardware.media.omx@1.0-service \
    manifest.lt013g.omx.xml
# END SM-T311 V161 COMMON

# BEGIN SM-T311 V195 STOCK GNSS BRIDGE
ifneq ($(filter lineage_lt013g,$(TARGET_PRODUCT)),)
PRODUCT_PACKAGES += \
    android.hardware.gnss@1.0-impl \
    android.hardware.gnss@1.0-service \
    manifest.lt013g.gnss.xml \
    gps.default.lt013g.v195 \
    lt013g-gps-legacy-launcher-v195 \
    liblt013g-gps-bridge-v290 \
    lt013g-gps-legacy-bridge-embedded \
    lt013g-gps-legacy-rootfs-v195

$(call inherit-product, device/samsung/lt013g/gps-v195/immutable-code.mk)

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.location.gps.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.location.gps.xml
endif
# END SM-T311 V195 STOCK GNSS BRIDGE


# BEGIN SM-T311 V165 VOLUME CURVES
PRODUCT_COPY_FILES += \
    frameworks/av/services/audiopolicy/config/audio_policy_volumes.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_volumes.xml \
    frameworks/av/services/audiopolicy/config/default_volume_tables.xml:$(TARGET_COPY_OUT_VENDOR)/etc/default_volume_tables.xml
# END SM-T311 V165 VOLUME CURVES

# BEGIN SM-T311 V167 STOCK AUDIO AND MESA
# END SM-T311 V167 STOCK AUDIO AND MESA

# BEGIN SM-T311 V168 PERFORMANCE
PRODUCT_PACKAGES += \
    android.hardware.power@1.0-service.lt013g

PRODUCT_COPY_FILES += \
    device/samsung/lt01-common/performance-v168/task_profiles.json:$(TARGET_COPY_OUT_VENDOR)/etc/task_profiles.json
# END SM-T311 V168 PERFORMANCE

# BEGIN SM-T311 V179 AUDIO INFRASTRUCTURE
PRODUCT_PACKAGES += \
    audio.usb.default \
    audio.r_submix.default \
    libalsautils \
    libnbaio_mono

PRODUCT_COPY_FILES += \
    device/samsung/lt01-common/audio-v179/a2dp_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/a2dp_audio_policy_configuration.xml \
    device/samsung/lt01-common/audio-v179/usb_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/usb_audio_policy_configuration.xml \
    device/samsung/lt01-common/audio-v179/r_submix_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/r_submix_audio_policy_configuration.xml \
    device/samsung/lt01-common/audio-v179/audio_policy_volumes.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_volumes.xml \
    device/samsung/lt01-common/audio-v179/default_volume_tables.xml:$(TARGET_COPY_OUT_VENDOR)/etc/default_volume_tables.xml \
    device/samsung/lt01-common/audio-v179/audio_policy_engine_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_engine_configuration.xml \
    device/samsung/lt01-common/audio-v179/audio_policy_engine_product_strategies.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_engine_product_strategies.xml \
    device/samsung/lt01-common/audio-v179/audio_policy_engine_stream_volumes.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_engine_stream_volumes.xml \
    device/samsung/lt01-common/audio-v179/audio_policy_engine_default_stream_volumes.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_engine_default_stream_volumes.xml \
    device/samsung/lt01-common/audio-v179/audio_effects.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_effects.xml
# END SM-T311 V179 AUDIO INFRASTRUCTURE




# BEGIN SM-T311 V183 MEDIA AUDIO PIPELINE
PRODUCT_COPY_FILES += \
    device/samsung/lt01-common/media-v183/media_profiles_V1_0.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_profiles_V1_0.xml \
    device/samsung/lt01-common/media-v183/media_codecs.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs.xml \
    device/samsung/lt01-common/media-v183/media_codecs_google_audio.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_google_audio.xml \
    device/samsung/lt01-common/media-v183/media_codecs_samsung_video.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_samsung_video.xml \
    device/samsung/lt01-common/media-v183/secomxregistry:$(TARGET_COPY_OUT_SYSTEM)/etc/secomxregistry

# G1160: This V183 block retains accepted OMX software audio and Samsung
# video fallback codecs. It does NOT disable Codec2: the HOTFIX26 block
# below enables the G1153-validated c2.v4l2.avc encoder/decoder on Linux 7.3.
# Legacy historical comments in SHA-pinned media_codecs.xml are NOT runtime
# configuration; do not modify that accepted physical XML just for wording.
# END SM-T311 V183 MEDIA AUDIO PIPELINE

# SM-T311 V188 BEGIN
PRODUCT_PACKAGES += audio.primary.lt013g
PRODUCT_PACKAGES += audio.usb.default audio.r_submix.default libalsautils libnbaio_mono
PRODUCT_PACKAGES += tinymix tinyplay tinycap
PRODUCT_COPY_FILES += device/samsung/lt01-common/audio-v193.1/audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_configuration.xml
DEVICE_PACKAGE_OVERLAYS += device/samsung/lt01-common/overlay
# SM-T311 V188 END

# BEGIN SM-T311 A2 BOOTCHAIN FIX
# Preserve A1 thermal integration, but restore the proven Android second-stage
# mount chain from the working system. EFS is an added late-mount entry.
PRODUCT_PACKAGES += \
    android.hardware.thermal@2.0-service.samsung

PRODUCT_COPY_FILES += \
    device/samsung/lt01-common/thermal/thermal_info_config.json:$(TARGET_COPY_OUT_VENDOR)/etc/thermal_info_config.json \
    device/samsung/lt01-common/rootdir/fstab.lt013g.vendor:$(TARGET_COPY_OUT_VENDOR)/etc/fstab.lt013g \
    device/samsung/lt01-common/rootdir/init.lt013g.vendor.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/hw/init.lt013g.rc
# END SM-T311 A2 BOOTCHAIN FIX

# BEGIN SM-T311 A3 KNOWN-GOOD RECONCILIATION
PRODUCT_PACKAGES += \
    android.hardware.audio.service \
    android.hardware.audio@6.0-impl \
    android.hardware.audio.effect@6.0-impl \
    android.hardware.bluetooth@1.1-service.btlinux \
    android.hardware.health@2.1-service \
    android.hardware.health@2.1-impl \
    android.hardware.keymaster@4.0-service \
    android.hardware.usb.gadget@1.1-service

PRODUCT_PACKAGES += smt311-usb-v115-source-g807

PRODUCT_COPY_FILES += \
    device/samsung/lt01-common/known-good-v20260730/init.lt013g.usb-v110.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/init.lt013g.usb-v110.rc \
    device/samsung/lt01-common/rootdir/init.lt013g.usb-functionfs.rc:$(TARGET_COPY_OUT_SYSTEM)/etc/init/init.lt013g.usb-functionfs.rc \
    device/samsung/lt01-common/rootdir/init.lt013g.adb-wifi.rc:$(TARGET_COPY_OUT_SYSTEM)/etc/init/init.lt013g.adb-wifi.rc \
    device/samsung/lt01-common/rootdir/init.lt013g.udc-uevent.rc:$(TARGET_COPY_OUT_SYSTEM)/etc/init/init.lt013g.udc-uevent.rc \
    device/samsung/lt01-common/rootdir/init.lt013g.usb-mtp-access.rc:$(TARGET_COPY_OUT_SYSTEM)/etc/init/init.lt013g.usb-mtp-access.rc \
    device/samsung/lt01-common/known-good-v20260730/init.lt013g.performance-v168-stage2.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/init.lt013g.performance-v168-stage2.rc \
    device/samsung/lt01-common/known-good-v20260730/init.lt013g.performance-v168-stage2-fix1.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/init.lt013g.performance-v168-stage2-fix1.rc \
    device/samsung/lt01-common/known-good-v20260730/android.hardware.wifi.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.xml

PRODUCT_COPY_FILES += \

PRODUCT_PACKAGES += \
    smt311_wpa_supplicant_known_good

# END SM-T311 A3 KNOWN-GOOD RECONCILIATION

# BEGIN SM-T311 HOTFIX26 MODERN V4L2 CODEC2
PRODUCT_SOONG_NAMESPACES += external/v4l2_codec2

PRODUCT_PACKAGES += \
    android.hardware.media.c2@1.0-service-v4l2 \
    libc2plugin_store \
    libv4l2_codec2_components \
    libcodec2_hidl@1.0 \
    libcodec2_hidl@1.1 \
    libcodec2_vndk \
    libstagefright_bufferqueue_helper_novndk \
    libcodec2_hidl_plugin_stub \
    libavservices_minijail.vendor

PRODUCT_COPY_FILES += \
    device/samsung/lt01-common/media-v183/media_codecs_c2.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_c2.xml \
    device/samsung/lt01-common/media-v183/codec2.vendor.ext.policy:$(TARGET_COPY_OUT_VENDOR)/etc/seccomp_policy/codec2.vendor.ext.policy \
    device/samsung/lt01-common/media-v183/init.t311.codec2-v4l2.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/init.t311.codec2-v4l2.rc

# Keep Codec2 fully visible so the V4L2 store keeps its hardware rank (0x80).
# G1160: Linux 7.3.0-pjarczak DMA-BUF/minigbm: Android11 Codec2 uses
# the accepted BLOB pool mask, while V4L2 MFC imports DMA-BUF. Do not
# regress to ION allocator assumptions or disable hardware C2.
PRODUCT_SYSTEM_DEFAULT_PROPERTIES += \
    debug.stagefright.ccodec=4 \
    debug.stagefright.c2-poolmask=0xfc0000 \
    debug.stagefright.omx_default_rank.sw-audio=4

PRODUCT_VENDOR_PROPERTIES += \
    ro.vendor.v4l2_codec2.encode_concurrent_instances=1 \
    ro.vendor.v4l2_codec2.decode_concurrent_instances=1
# END SM-T311 HOTFIX26 MODERN V4L2 CODEC2

# BEGIN SM-T311 PLATFORM V3 VIBRATOR
ifneq ($(filter lineage_lt013g,$(TARGET_PRODUCT)),)
PRODUCT_PACKAGES += \
    android.hardware.vibrator@1.0-service.lt013g
endif
# END SM-T311 PLATFORM V3 VIBRATOR

# BEGIN SM-T311 V4 COVER
PRODUCT_COPY_FILES += \
    device/samsung/lt01-common/rootdir/init.lt013g.cover.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/init.lt013g.cover.rc \
    device/samsung/lt01-common/rootdir/lt013g-cover-defaults.sh:$(TARGET_COPY_OUT_VENDOR)/etc/lt013g-cover-defaults.sh
# END SM-T311 V4 COVER

# BEGIN SM-T311 V5 DEBUG
PRODUCT_COPY_FILES += \
    device/samsung/lt01-common/rootdir/init.lt013g.debug.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/init.lt013g.debug.rc
# END SM-T311 V5 DEBUG

# Offline recovery diagnostics only - not part of normal builds.
# ifneq ($(filter lineage_lt013g,$(TARGET_PRODUCT)),)
# PRODUCT_COPY_FILES += device/samsung/lt01-common/rootdir/init.lt013g.reboot-archive.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/init.lt013g.reboot-archive.rc \
#     device/samsung/lt01-common/rootdir/init.lt013g.reboot-trace-cache.rc:$(TARGET_COPY_OUT_SYSTEM)/etc/init/init.lt013g.reboot-trace-cache.rc \
#     device/samsung/lt01-common/rootdir/init.lt013g.g481-boot-capture.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/init.lt013g.g481-boot-capture.rc
# endif
