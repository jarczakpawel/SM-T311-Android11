include device/samsung/lt01-common/BoardConfigCommon.mk

DEVICE_PATH := device/samsung/lt013g

# Preserve executable mode for stock GNSS binaries stored in vendor/etc.
TARGET_FS_CONFIG_GEN += device/samsung/lt013g/config.fs

TARGET_OTA_ASSERT_DEVICE := lt013g,lt013gxx,SM-T311

# Parser stage only. v49 will integrate the audited Linux 6.12 kernel,
# source-built DTB and modules. This prevents accidental boot.img creation
# from an incomplete Android device tree.
# TARGET_NO_KERNEL removed by audited v56 integration

# XMM6262 legacy HIDL radio service.
DEVICE_MANIFEST_FILE += device/samsung/lt013g/radio/manifest.radio.xml

# G1139: pin the exact ARM32 zImage embedded in physical G1130 BOOT.
# Custom release packaging separately pins the entire accepted BOOT image.
TARGET_PREBUILT_KERNEL := device/samsung/lt013g/prebuilt/kernel-7.3.0-pjarczak-G1130-VERIFIED-zImage.bin

BUILD_BROKEN_NINJA_USES_ENV_VARS += MKE2FS_CONFIG

# BEGIN SM-T311 PJARCZAK V214 CAMERA VINTF
DEVICE_MANIFEST_FILE += device/samsung/lt013g/camera-v214/manifest.camera-v214.xml
# END SM-T311 PJARCZAK V214 CAMERA VINTF
DEVICE_MANIFEST_FILE += device/samsung/lt01-common/known-good-v20260730/android.hardware.audio@6.0.xml

T311_SOURCE_GUARD_STATUS := $(shell python3 device/samsung/lt013g/tools/g1139-verify-pinned-g1130-release.py --source --boot >/dev/null 2>&1 && echo PASS)
ifneq ($(T311_SOURCE_GUARD_STATUS),PASS)
$(error SM-T311 G1139: verified G1130 source/BOOT guard failed)
endif
