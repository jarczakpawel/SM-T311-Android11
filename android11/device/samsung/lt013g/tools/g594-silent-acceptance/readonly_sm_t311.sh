#!/system/bin/sh
# G594: silent/readonly SM-T311 system health capture.
# No sound, camera, GPS position requests, modem calls, network writes,
# settings edits, ADB daemon toggles, root, install, reboot or notifications.
export LC_ALL=C
echo G594_BEGIN
echo BOOT_COMPLETED="$(getprop sys.boot_completed)"
echo KERNEL_RELEASE="$(uname -r)"
echo SELINUX_MODE="$(getenforce 2>/dev/null)"
system="$(getprop ro.system.build.fingerprint)"
vendor="$(getprop ro.vendor.build.fingerprint)"
if [ -n "$system" ] && [ "$system" = "$vendor" ];then
  echo SYSTEM_VENDOR_FINGERPRINT_MATCH=1
else
  echo SYSTEM_VENDOR_FINGERPRINT_MATCH=0
fi
# Do not expose EFS, phone number, IMSI, IMEI or AP/BSSID/key identifiers.
echo RADIO_DAEMON="$(getprop init.svc.ril-daemon)"
echo CPBOOT_DAEMON="$(getprop init.svc.cpboot-daemon)"
echo CP_BOOT_COMPLETE="$(getprop ril.cbd.boot_done)"
echo SIM_STATE_NONEMPTY="$([ -n "$(getprop gsm.sim.state)" ] && echo 1 || echo 0)"
echo GPS_DAEMON="$(getprop init.svc.vendor.lt013g-gps-legacy-v195)"
echo USB_GADGET_FUNCTIONS="$(dumpsys usb 2>/dev/null | sed -n 's/^[[:space:]]*current_functions=//p' | head -1)"
echo USB_GADGET_APPLIED="$(dumpsys usb 2>/dev/null | sed -n 's/^[[:space:]]*current_functions_applied=//p' | head -1)"
echo USB_GADGET_CONFIGURED="$(dumpsys usb 2>/dev/null | sed -n 's/^[[:space:]]*configured=//p' | head -1)"
echo USB_SCREEN_UNLOCKED_FUNCTIONS="$(dumpsys usb 2>/dev/null | sed -n 's/^[[:space:]]*screen_unlocked_functions=//p' | head -1)"
echo ADB_USB_DAEMON="$(getprop init.svc.adbd)"
echo SECURE_ADB_WIFI_SWITCH="$(settings get global adb_wifi_enabled 2>/dev/null)"
echo SECURE_ADB_TLS_PORT="$(getprop service.adb.tls.port)"
echo TRUSTED_WIFI_AP_COUNT="$(dumpsys adb 2>/dev/null | grep -c '<wifiAP ' 2>/dev/null)"
echo ZRAM_SWAP_KIB="$(sed -n 's/^SwapTotal:[ ]*\([0-9]*\) kB$/\1/p' /proc/meminfo)"
echo ZRAM_FREE_KIB="$(sed -n 's/^SwapFree:[ ]*\([0-9]*\) kB$/\1/p' /proc/meminfo)"
echo MEM_AVAILABLE_KIB="$(sed -n 's/^MemAvailable:[ ]*\([0-9]*\) kB$/\1/p' /proc/meminfo)"
echo CPU_GOVERNOR="$(cat /sys/devices/system/cpu/cpufreq/policy0/scaling_governor 2>/dev/null)"
echo CPU0_FREQ_KHZ="$(cat /sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq 2>/dev/null)"
echo CPUFREQ_TOTAL_TRANSITIONS="$(cat /sys/devices/system/cpu/cpufreq/policy0/stats/total_trans 2>/dev/null)"
echo CPUFREQ_200MHZ_TIME_TICKS="$(awk '$1==200000{print $2}' /sys/devices/system/cpu/cpufreq/policy0/stats/time_in_state 2>/dev/null)"
echo CPUFREQ_1400MHZ_TIME_TICKS="$(awk '$1==1400000{print $2}' /sys/devices/system/cpu/cpufreq/policy0/stats/time_in_state 2>/dev/null)"
# Classify counts only. No raw log lines may include private data.
# Stream logs into grep: never store many kilobytes in a shell variable,
# which can trigger Android /system/bin/printf "Argument list too long".
echo KERNEL_FATAL_COUNT="$(logcat -d -b kernel -t 3000 -v brief 2>/dev/null | grep -Ec 'Kernel panic|BUG: unable to handle|Oops:')"
echo DWC2_ENDPOINT_STOP_TIMEOUT_COUNT="$(logcat -d -b kernel -t 3000 -v brief 2>/dev/null | grep -c 'dwc2_hsotg_ep_stop_xfr: timeout')"
echo EXYNOS_OPP_REJECT_COUNT="$(logcat -d -b kernel -t 3000 -v brief 2>/dev/null | grep -c 'SMT311_G582_OPP_REJECT')"
echo CRASH_FATAL_SIGNAL_COUNT="$(logcat -d -b crash -t 500 -v brief 2>/dev/null | grep -c 'Fatal signal')"
echo G594_END
