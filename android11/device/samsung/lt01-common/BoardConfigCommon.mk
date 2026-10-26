COMMON_PATH := device/samsung/lt01-common

# 32-bit Exynos 4212 / Cortex-A9.
TARGET_ARCH := arm
TARGET_ARCH_VARIANT := armv7-a-neon
TARGET_CPU_VARIANT := cortex-a9
TARGET_CPU_ABI := armeabi-v7a
TARGET_CPU_ABI2 := armeabi
TARGET_CPU_SMP := true
TARGET_SUPPORTS_32_BIT_APPS := true
TARGET_SUPPORTS_64_BIT_APPS := false

TARGET_BOARD_PLATFORM := exynos4
TARGET_BOOTLOADER_BOARD_NAME := smdk4x12
TARGET_NO_BOOTLOADER := true
TARGET_NO_RADIOIMAGE := true

# Confirmed stock boot header v0 geometry. The ramdisk placement remains
# blocked until the bootloader mapping is proven, so v48 builds no boot.img.

# Physical partition limits from the audited SM-T311 layout.
BOARD_BOOTIMAGE_PARTITION_SIZE := 8388608
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 8388608
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 2235564032
BOARD_CACHEIMAGE_PARTITION_SIZE := 427819008
BOARD_USERDATAIMAGE_PARTITION_SIZE := 12813598720

TARGET_USERIMAGES_USE_EXT4 := true
BOARD_CACHEIMAGE_FILE_SYSTEM_TYPE := ext4

# Flash block geometry and final image layout are intentionally deferred
# until the stock PIT and image builders are validated together.

# SM-T311 Android 11 performance properties.
TARGET_SYSTEM_PROP += device/samsung/lt01-common/system.prop

# BEGIN SM-T311 V56 BOOT GEOMETRY
# v107 source-kernel disabled: TARGET_PREBUILT_KERNEL := device/samsung/lt01-common/prebuilt/v168/kernel
BOARD_BOOT_HEADER_VERSION := 0
BOARD_KERNEL_BASE := 0x10000000
BOARD_KERNEL_PAGESIZE := 2048
BOARD_KERNEL_OFFSET := 0x00008000
BOARD_RAMDISK_OFFSET := 0x02000000
BOARD_SECOND_OFFSET := 0x00f00000
BOARD_TAGS_OFFSET := 0x00000100
BOARD_KERNEL_TAGS_OFFSET := 0x00000100
BOARD_KERNEL_CMDLINE := console=ttySAC2,115200 androidboot.hardware=lt013g
# Optional operator-only recovery build: make init publish
# ro.boot.t311.sf_force_gpu_client=1 from boot.img kernel command line.
# DEFAULT OFF: no change to the released kernel cmdline or normal HWC path.
ifeq ($(SMT311_SAFE_GPU_BOOT),true)
BOARD_KERNEL_CMDLINE += androidboot.t311.sf_force_gpu_client=1
endif
BOARD_MKBOOTIMG_ARGS := --kernel_offset 0x00008000 --ramdisk_offset 0x02000000 --second_offset 0x00f00000 --tags_offset 0x00000100
# END SM-T311 V56 BOOT GEOMETRY

# BEGIN SM-T311 V59 MAINLINE DRM LIMA
# Open Linux 6.12 DRM/KMS + Mesa Lima path. No Mali UMP blobs.
BOARD_DRM_HWCOMPOSER_BUFFER_IMPORTER := minigbm
DEVICE_MANIFEST_FILE += device/samsung/lt01-common/manifest.graphics.xml
# END SM-T311 V59 MAINLINE DRM LIMA
# SM-T311 V151 - Android Light HAL
BOARD_VENDOR_SEPOLICY_DIRS += device/samsung/lt01-common/sepolicy/vendor
BOARD_PLAT_PRIVATE_SEPOLICY_DIR += device/samsung/lt01-common/sepolicy/private

# BEGIN SM-T311 V158 SENSORS VINTF
DEVICE_MANIFEST_FILE += device/samsung/lt01-common/manifest.sensors.xml
# END SM-T311 V158 SENSORS VINTF

# BEGIN SM-T311 V167 MESA
BOARD_GPU_DRIVERS := lima kmsro
# END SM-T311 V167 MESA

# BEGIN SM-T311 XMM6262 RADIO
# Select Samsung's legacy-compatible libril provider for the XMM6262 modem.
BOARD_PROVIDES_LIBRIL := true
# G1139: do not replace physically accepted modem RIL with a divergent
# unqualified rebuild when assembling Android 11 for SM-T311.
SMT311_PIN_ACCEPTED_LIBRIL := true
BOARD_MODEM_TYPE := xmm6262
# END SM-T311 XMM6262 RADIO

# SM-T311 Samsung hardware provider gate
BOARD_VENDOR := samsung


# BEGIN SM-T311 RECOVERY FSTAB V109
TARGET_RECOVERY_FSTAB := device/samsung/lt013g/rootdir/recovery-ota.fstab
# END SM-T311 RECOVERY FSTAB V109

# BEGIN SM-T311 PJARCZAK KERNEL BUILD
TARGET_KERNEL_SOURCE := kernel/samsung/lt013g
TARGET_KERNEL_CONFIG := sm_t311_android11_defconfig
TARGET_FORCE_PREBUILT_KERNEL := true
TARGET_PREBUILT_KERNEL := device/samsung/lt01-common/prebuilt/pjarczak/kernel
# END SM-T311 PJARCZAK KERNEL BUILD

# BEGIN SM-T311 PJARCZAK RECOVERY OTA
BOARD_USES_FULL_RECOVERY_IMAGE := true
BOARD_CUSTOM_BOOTIMG_MK := device/samsung/lt013g/t311-custom-bootimg.mk
# Package the exact prebuilt BOOT and TWRP RECOVERY into target-files/BOOTABLE_IMAGES.
# Otherwise add_img_to_target_files.py regenerates a 13+ MiB recovery that
# exceeds the physical 8 MiB /dev/block/mmcblk0p10 partition.
BOARD_CUSTOM_BOOTIMG := true
# END SM-T311 PJARCZAK RECOVERY OTA


# SM-T311 HOTFIX26 modern V4L2 Codec2
DEVICE_MANIFEST_FILE += device/samsung/lt01-common/manifest.codec2-v4l2.xml
