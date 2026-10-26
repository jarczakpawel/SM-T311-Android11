# SM-T311 G507: preserve exact OEM-compatible proven Android11 WM1811 HAL.
# Physically installed and tested V191 SHA256:
# 636743a098acb4ab31bad39adb3c46875c461d4b313f5fddb6c2106318224cfa
# Experimental HAL sources retained but NOT included in production target_files.
LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE := audio.primary.lt013g
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_MODULE_SUFFIX := .so
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_VENDOR_MODULE := true
LOCAL_SRC_FILES := audio.primary.lt013g.so
LOCAL_SHARED_LIBRARIES := liblog libcutils libhardware libtinyalsa
LOCAL_STRIP_MODULE := false
include $(BUILD_PREBUILT)
