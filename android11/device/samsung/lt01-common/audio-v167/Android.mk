LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := audio.primary.lt013g
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_VENDOR_MODULE := true
LOCAL_SRC_FILES := audio_hw.c
LOCAL_SHARED_LIBRARIES := \
    liblog \
    libcutils \
    libtinyalsa
LOCAL_HEADER_LIBRARIES := libhardware_headers
LOCAL_C_INCLUDES := \
    external/tinyalsa/include \
    system/media/audio_utils/include \
    system/media/audio_effects/include
LOCAL_CFLAGS := \
    -Wall \
    -Wextra \
    -Werror=implicit-function-declaration \
    -Wno-unused-parameter \
    -Wno-missing-field-initializers
include $(BUILD_SHARED_LIBRARY)
