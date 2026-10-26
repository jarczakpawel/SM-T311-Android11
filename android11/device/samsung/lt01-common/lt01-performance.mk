# SM-T311 / Android 11 selective low-memory optimizations.
#
# Deliberately does NOT set ro.config.low_ram=true. That switch changes
# framework graphics behavior and will only be evaluated in a later A/B test.

PRODUCT_SYSTEM_SERVER_COMPILER_FILTER := speed
PRODUCT_ALWAYS_PREOPT_EXTRACTED_APK := true
PRODUCT_USE_PROFILE_FOR_BOOT_IMAGE := true
PRODUCT_DEX_PREOPT_BOOT_IMAGE_PROFILE_LOCATION := frameworks/base/config/boot-image-profile.txt
PRODUCT_ART_TARGET_INCLUDE_DEBUG_BUILD := false
PRODUCT_MINIMIZE_JAVA_DEBUG_INFO := true
TARGET_VNDK_USE_CORE_VARIANT := true

# TARGET_SYSTEM_PROP is set in BoardConfigCommon.mk. This file stays product-only.
