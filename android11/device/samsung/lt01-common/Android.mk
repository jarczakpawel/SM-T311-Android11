LOCAL_PATH := $(call my-dir)

# Empty by design at the parser foundation stage.

# BEGIN SM-T311 V165 AUDIO MODULE DISCOVERY
V165_LT01_COMMON_PATH := $(call my-dir)
# END SM-T311 V165 AUDIO MODULE DISCOVERY

# BEGIN SM-T311 V167 MODULE DISCOVERY
V167_LT01_COMMON_PATH := $(call my-dir)
# END SM-T311 V167 MODULE DISCOVERY

# SM-T311 V187 stock-stable primary audio HAL

# G973: the previous G507 golden prebuilt hid all subsequent changes
# to the actual audio-v193.1/audio_hw.c. For this new source-qualified
# branch, select exactly ONE real 48k WM1811 audio HAL producer.
# Do not edit a file that is not compiled; audio-golden remains unchanged
# in Git as the previous hardware-accepted baseline.
ifeq ($(TARGET_DEVICE),lt013g)
include $(call my-dir)/audio-v193.1/Android.mk
endif

# Restore common device module location after including the audio submakefile.
LOCAL_PATH := $(V167_LT01_COMMON_PATH)
# G867: one ARM32 libdrm_mesa26 producer for the pinned Mesa26.2.5 GBM.
# The GBM prebuilt declares this as a real shared-library dependency, so
# Android's ELF checker keeps validating its DT_NEEDED list (no bypass).
ifeq ($(TARGET_DEVICE),lt013g)
ifeq ($(SMT311_USE_MESA26),true)
ifeq ($(SMT311_MESA26_VERSION),26.2.5)
include $(CLEAR_VARS)
LOCAL_MODULE := libdrm_mesa26
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_MODULE_SUFFIX := .so
LOCAL_STRIP_MODULE := false
LOCAL_VENDOR_MODULE := true
LOCAL_SRC_FILES := prebuilt/pjarczak/mesa26-2625/libdrm_mesa26.so
include $(BUILD_PREBUILT)
endif
endif
endif


ifeq ($(SMT311_USE_MESA26),true)
MESA26_GALLIUM_DRI_LINK := $(TARGET_OUT_VENDOR)/lib/dri/gallium_dri.so
$(MESA26_GALLIUM_DRI_LINK): $(TARGET_OUT_VENDOR)/lib/libgallium_dri.so
	@mkdir -p $(dir $@)
	@ln -sfn ../libgallium_dri.so $@
ALL_DEFAULT_INSTALLED_MODULES += $(MESA26_GALLIUM_DRI_LINK)
MESA26_LIMA_DRI_LINK := $(TARGET_OUT_VENDOR)/lib/dri/lima_dri.so
$(MESA26_LIMA_DRI_LINK): $(MESA26_GALLIUM_DRI_LINK)
	@ln -sfn gallium_dri.so $@
ALL_DEFAULT_INSTALLED_MODULES += $(MESA26_LIMA_DRI_LINK)
endif
