#!/system/bin/sh
# G583 read-only privacy-limited postboot acceptance. No writes or root.
# No raw radio, SIM, network IDs, location coordinates or user content.
export LC_ALL=C
echo G583_AUDIT_START
echo OS_BOOT_COMPLETED="$(getprop sys.boot_completed)"
echo KERNEL_RELEASE="$(uname -r)"
echo SELINUX_MODE="$(getenforce 2>/dev/null)"
SYSTEM_FP="$(getprop ro.system.build.fingerprint)"
VENDOR_FP="$(getprop ro.vendor.build.fingerprint)"
if [ "$SYSTEM_FP" = "$VENDOR_FP" ] && [ -n "$SYSTEM_FP" ];then echo SYSTEM_VENDOR_FINGERPRINT=MATCH;else echo SYSTEM_VENDOR_FINGERPRINT=MISMATCH;fi
echo USB_FUNCTIONS="$(getprop sys.usb.state)"
echo NATIVE_ADB_TLS_ENABLED="$(settings get global adb_wifi_enabled 2>/dev/null)"
echo ZRAM_SWAP_KIB="$(sed -n 's/^SwapTotal:[ ]*\([0-9]*\) kB$/\1/p' /proc/meminfo)"
echo AVAILABLE_RAM_KIB="$(sed -n 's/^MemAvailable:[ ]*\([0-9]*\) kB$/\1/p' /proc/meminfo)"
echo RIL_SERVICE="$(getprop init.svc.ril-daemon)"
echo AUDIO_SERVER="$(getprop init.svc.audioserver)"
echo GPS_LEGACY_HAL="$(getprop init.svc.vendor.lt013g-gps-legacy-v195)"
echo GPU_RENDERER_BEGIN
dumpsys SurfaceFlinger 2>/dev/null | grep '^GLES: ' | head -1
dumpsys SurfaceFlinger 2>/dev/null | grep '^HWC missed frame count: ' | head -1
echo GPU_RENDERER_END
echo CAMERAS_DETECTED_BEGIN
dumpsys media.camera 2>/dev/null | grep '^Number of camera devices:' | head -1
echo CAMERAS_DETECTED_END
echo SENSORS_REGISTERED_BEGIN
dumpsys sensorservice 2>/dev/null | grep -E 'CM36653 Ambient Light|CM36653 Proximity|LIS2DH12 Accelerometer|YAS532 Magnetic Field' | head -4
echo SENSORS_REGISTERED_END
echo GNSS_SERVICE_STATUS_BEGIN
dumpsys location 2>/dev/null | grep -E 'mStarted=|Total number of sv status messages processed:' | head -5
echo GNSS_SERVICE_STATUS_END
echo BATTERY_STATUS_BEGIN
dumpsys battery 2>/dev/null | grep -E '^[ ]+(AC powered|USB powered|Wireless powered|status|level|temperature):' | head -6
echo BATTERY_STATUS_END
echo G583_AUDIT_END
