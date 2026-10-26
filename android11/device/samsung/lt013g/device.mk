LOCAL_PATH := device/samsung/lt013g

# G867 release lock: match the physically installed, SHA-pinned Mesa 26.2.5.
# This device targets only 26.2.5; obsolete 26.2.4 is not a build option.
SMT311_USE_MESA26 := true
SMT311_MESA26_VERSION := 26.2.5

# Common SM-T31x hardware foundation.
$(call inherit-product, device/samsung/lt01-common/common.mk)

# Device-specific files and HALs are intentionally added only after their
# Android 11 ABI, init, SELinux and hardware contracts are verified.

# BEGIN SM-T311 FULL PLATFORM V107
$(call inherit-product, device/samsung/lt013g/t311-platform.mk)
# END SM-T311 FULL PLATFORM V107

PRODUCT_COPY_FILES += \
    device/samsung/lt013g/rootdir/recovery-ota.fstab:recovery/root/system/etc/recovery.fstab
