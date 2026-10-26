LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := audio.primary.lt013g
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_VENDOR_MODULE := true
LOCAL_SRC_FILES := audio_hw.c t311_ril_audio.c
LOCAL_SHARED_LIBRARIES := \
    liblog \
    libcutils \
    libhardware \
    libtinyalsa
LOCAL_C_INCLUDES := \
    external/tinyalsa/include \
    system/media/audio/include
LOCAL_CFLAGS := -Wall -Wextra -Werror -Wno-unused-parameter
include $(BUILD_SHARED_LIBRARY)
