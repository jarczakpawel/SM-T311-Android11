# Samsung Galaxy Tab 3 8.0 LTE SM-T311 - LineageOS 18.1 product.

# G1410 — SM-T311 only: tell imported AOSP/Lineage product makefiles to
# omit the optional DSU GSI installer and Seedvault backup front end.
# These flags are intentionally set BEFORE inherit-product is processed.
# Actual get_build_var PRODUCT_PACKAGES must confirm both are absent.
SMT311_EXCLUDE_DSU := true
SMT311_EXCLUDE_SEEDVAULT := true
# G1778: Traceur is a tracing front-end, not needed in the final public ROM.
SMT311_EXCLUDE_TRACEUR := true
SMT311_EXCLUDE_CTS_SHIMS := true
SMT311_EXCLUDE_EASTEREGG := true
SMT311_RELEASE_STRIP_DEBUG_TOOLS := true

# 32-bit Android with telephony. This is the pattern used by maintained
# ARM32 LineageOS 18.1 devices.
$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base_telephony.mk)

# Device configuration.
$(call inherit-product, device/samsung/lt013g/device.mk)

# LineageOS phone feature set. SM-T311 contains the XMM6262 modem.
TARGET_EXCLUDES_AUDIOFX := true
TARGET_EXCLUDES_UPDATER := true
# BEGIN SM-T311 PJARCZAK BUILD ID
TARGET_UNOFFICIAL_BUILD_ID := PJARCZAK
# END SM-T311 PJARCZAK BUILD ID

$(call inherit-product, vendor/lineage/config/common_full_phone.mk)

# This Android 11 legacy device does not discover the MediaProvider APK inside
# the mounted APEX, so install the SAME source-built provider as a priv-app.
# The APEX still supplies framework-mediaprovider.jar and native FUSE libs.
PRODUCT_PACKAGES += MediaProvider

TARGET_BOOTANIMATION_PRELOAD := true
TARGET_BOOTANIMATION_TEXTURE_CACHE := true
TARGET_SCREEN_HEIGHT := 1280
TARGET_SCREEN_WIDTH := 800

PRODUCT_NAME := lineage_lt013g
PRODUCT_DEVICE := lt013g
PRODUCT_BRAND := samsung
PRODUCT_MODEL := SM-T311
PRODUCT_MANUFACTURER := samsung
PRODUCT_GMS_CLIENTID_BASE := android-samsung

PRODUCT_BUILD_PROP_OVERRIDES += \
    PRODUCT_NAME=lt013gxx \
    TARGET_DEVICE=lt013g

# G1410 source-gate keeps print/telephony/media stack. An inherited package
# CANNOT be deleted here via filter-out: product inherit lists are merged after
# expansion. Instead honor SMT311_EXCLUDE_* at both package *definitions*.
# G1410: AOSP/Lineage boot-dexpreopt for preinstalled launchers. ART can
# still override it when a Launcher3 APK is installed later under /data;
# G1400 is the explicit, source-versioned post-install provisioning step.
PRODUCT_DEXPREOPT_SPEED_APPS += Launcher3 TrebuchetQuickStep

# Keep the original V187 normalization as a no-op (not a package removal).
PRODUCT_PACKAGES := $(filter-out ,$(PRODUCT_PACKAGES))
