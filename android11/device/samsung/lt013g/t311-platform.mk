# SM-T311 device-specific overlays.

# G1781: live PID 547 wpa_supplicant maps /system/lib/libnl.so.
# The removed userdebug CLI tools used to pull this shared library in.
# Retain it explicitly. The active vibrator@1.0 HIDL HAL remains unchanged.
PRODUCT_PACKAGES += libnl

# G1784: guaranteed quiet defaults from SYSTEM/build.prop even when an
# accepted, checksum-pinned BOOT image contains an older root/default.prop.
# ADB WiFi stays enabled by its separate persist.adb.tcp.port=5555 property.
PRODUCT_SYSTEM_PROPERTIES += \
    persist.traced.enable=0 \
    debug.t311.mesa_glpb=0 \
    debug.t311.hwui_draw_stages=0

DEVICE_PACKAGE_OVERLAYS += device/samsung/lt013g/overlay

# SM-T311 PJARCZAK platform payload.
PRODUCT_PACKAGES += \
    t311-cbd \
    t311-audio-ril \
    t311-platform-init \
    t311-recovery-noop \
    libril \
    t311-libsec-ril \
    t311-libsecnativefeature \
    t311-libstlport \
    t311-libcutils-compat \
    t311-libalarm-compat
PRODUCT_PACKAGES += t311-modem-watchdog t311-radio-hal-probe t311-mobile-session-probe t311-cbd-pidfd-signal
PRODUCT_PACKAGES += Stk

# G288: system MediaProvider must retain Android 11 Mainline private APIs.
PRODUCT_COPY_FILES += \
    device/samsung/lt013g/sysconfig/lt013g-mediaprovider-hiddenapi.xml:$(TARGET_COPY_OUT_SYSTEM)/etc/sysconfig/lt013g-mediaprovider-hiddenapi.xml
PRODUCT_PACKAGES += \
    libfimc \
    libfimg \
    libsecion \
    macloader
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.telephony.gsm.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.telephony.gsm.xml \
    frameworks/native/data/etc/android.hardware.camera.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.xml \
    frameworks/native/data/etc/android.hardware.camera.autofocus.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.autofocus.xml \
    frameworks/native/data/etc/android.hardware.camera.front.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.front.xml \
    frameworks/native/data/etc/android.hardware.location.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.location.xml \
    frameworks/native/data/etc/android.hardware.location.gps.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.location.gps.xml \
    frameworks/native/data/etc/android.hardware.sensor.accelerometer.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.accelerometer.xml \
    frameworks/native/data/etc/android.hardware.sensor.compass.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.compass.xml \
    frameworks/native/data/etc/android.hardware.sensor.light.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.light.xml \
    frameworks/native/data/etc/android.hardware.sensor.proximity.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.proximity.xml \
    frameworks/native/data/etc/android.hardware.bluetooth_le.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.bluetooth_le.xml \
    frameworks/native/data/etc/android.hardware.touchscreen.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.touchscreen.xml \
    frameworks/native/data/etc/android.hardware.touchscreen.multitouch.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.touchscreen.multitouch.xml \
    frameworks/native/data/etc/android.hardware.touchscreen.multitouch.jazzhand.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.touchscreen.multitouch.jazzhand.xml \
    frameworks/native/data/etc/android.hardware.usb.host.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.usb.host.xml \
    frameworks/native/data/etc/android.hardware.wifi.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.xml \
    frameworks/native/data/etc/android.hardware.wifi.direct.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.direct.xml

# BEGIN SM-T311 PJARCZAK v187 SYSTEM-AS-ROOT EFS MOUNTPOINT
# The v185 boot ramdisk and vendor fstab both mount mmcblk0p3 on /efs.
# / is the read-only system-as-root image by second-stage init, so mkdir /efs
# cannot be relied on to create a missing top-level mountpoint. Bake it into ROOT.
PRODUCT_COPY_FILES += \
    device/samsung/lt013g/rootfs/efs/.t311_mountpoint:$(TARGET_COPY_OUT_ROOT)/efs/.t311_mountpoint
# END SM-T311 PJARCZAK v187 SYSTEM-AS-ROOT EFS MOUNTPOINT

# BEGIN SM-T311 PJARCZAK V214 DUAL CAMERA HAL3
PRODUCT_PACKAGES += \
    android.hardware.camera.provider@2.4-external \
    android.hardware.camera.provider@2.4-external-service \
    camera.device@3.2-impl \
    camera.device@3.3-impl \
    camera.device@3.4-impl \
    camera.device@3.5-impl \
    camera.device@3.4-external-impl \
    camera.device@3.5-external-impl \
    camera.device@3.6-external-impl

PRODUCT_COPY_FILES += \
    device/samsung/lt013g/camera-v214/external_camera_config.xml:$(TARGET_COPY_OUT_VENDOR)/etc/external_camera_config.xml \
    device/samsung/lt013g/camera-v214/init.t311.camera-nodes-v214.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/t311-camera-nodes-v214.rc

PRODUCT_VENDOR_PROPERTIES += \
    ro.vendor.camera.external.hal3TrebleMinorVersion=4
# END SM-T311 PJARCZAK V214 DUAL CAMERA HAL3

# BEGIN SM-T311 PJARCZAK V222 STOCK CAMERA2
# Keep exactly one normal camera application in the product.
PRODUCT_PACKAGES += \
    Camera2
# END SM-T311 PJARCZAK V222 STOCK CAMERA2

PRODUCT_PACKAGES += android.hardware.ir@1.0-service.lt013g
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.consumerir.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.consumerir.xml
