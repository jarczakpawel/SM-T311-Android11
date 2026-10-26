#!/system/bin/sh
# G588 passive postflash USB MTP and encrypted WiFi ADB status.
# No root, no writes, no network pairing. No keys, SSID or BSSID emitted.
export LC_ALL=C
echo G588_CHECK_START
echo BOOT_COMPLETED="$(getprop sys.boot_completed)"
systemfp="$(getprop ro.system.build.fingerprint)"
vendorfp="$(getprop ro.vendor.build.fingerprint)"
case "$systemfp" in *SMT311G586*) if [ "$systemfp" = "$vendorfp" ]; then echo ROM_FINGERPRINTS_EQUAL=PASS; else echo ROM_FINGERPRINTS_EQUAL=FAIL; fi;; *) echo ROM_FINGERPRINTS_EQUAL=FAIL;; esac
echo KERNEL_RELEASE="$(uname -r)"
echo SELINUX="$(getenforce 2>/dev/null)"
echo USB_CURRENT_FUNCTIONS="$(dumpsys usb 2>/dev/null | sed -n 's/^[[:space:]]*current_functions=//p' | head -1)"
echo USB_APPLIED="$(dumpsys usb 2>/dev/null | sed -n 's/^[[:space:]]*current_functions_applied=//p' | head -1)"
echo USB_CONFIGURED="$(dumpsys usb 2>/dev/null | sed -n 's/^[[:space:]]*configured=//p' | head -1)"
echo USB_SCREEN_UNLOCKED_FUNCTIONS="$(dumpsys usb 2>/dev/null | sed -n 's/^[[:space:]]*screen_unlocked_functions=//p' | head -1)"
echo USB_PROPERTY_CURRENT="$(getprop sys.usb.state)"
echo ADB_DAEMON="$(getprop init.svc.adbd)"
echo ENCRYPTED_WIFI_ADB_SUPPORTED="$(cmd adb is-wifi-supported 2>/dev/null)"
echo ENCRYPTED_WIFI_ADB_SWITCH="$(settings get global adb_wifi_enabled 2>/dev/null)"
echo TLS_ADBD_PORT="$(getprop service.adb.tls.port)"
echo TRUSTED_WIFI_NETWORK_COUNT="$(dumpsys adb 2>/dev/null | grep -c '<wifiAP ' 2>/dev/null)"
echo MTP_SERVER_STARTED_MARKER_COUNT="$(logcat -d -t 1000 -s MtpService:D '*:S' 2>/dev/null | grep -c 'starting MTP server' 2>/dev/null)"
echo G588_CHECK_END
