#!/system/bin/sh
# G598: passive USB/PMIC/devfreq and memory visibility, no hardware actions.
# No camera/audio/location/calls/SIM/network/notification/setting changes.
export LC_ALL=C
echo G598_BEGIN
echo BOOT_COMPLETED="$(getprop sys.boot_completed)"
echo KERNEL="$(uname -r)"
echo SELINUX="$(getenforce 2>/dev/null)"
system="$(getprop ro.system.build.fingerprint)"
vendor="$(getprop ro.vendor.build.fingerprint)"
if [ -n "$system" ] && [ "$system" = "$vendor" ]; then
    echo FINGERPRINT_MATCH=1
else
    echo FINGERPRINT_MATCH=0
fi
echo RIL_SERVICE="$(getprop init.svc.ril-daemon)"
echo CPBOOT_SERVICE="$(getprop init.svc.cpboot-daemon)"
echo GPS_SERVICE="$(getprop init.svc.vendor.lt013g-gps-legacy-v195)"
echo ADB_SERVICE="$(getprop init.svc.adbd)"
dumpsys usb 2>/dev/null | awk '
/^[[:space:]]*current_functions=/ { sub(/^[[:space:]]*/, ""); print "USB_" $0 }
/^[[:space:]]*current_functions_applied=/ { sub(/^[[:space:]]*/, ""); print "USB_" $0 }
/^[[:space:]]*screen_unlocked_functions=/ { sub(/^[[:space:]]*/, ""); print "USB_" $0 }
/^[[:space:]]*configured=/ { sub(/^[[:space:]]*/, ""); print "USB_" $0 }
'
echo MTP_HOST_FILE_COPY=UNVERIFIED
echo ADB_WIFI_SWITCH="$(settings get global adb_wifi_enabled 2>/dev/null)"
echo ADB_WIFI_TLS_PORT="$(getprop service.adb.tls.port)"
echo ADB_WIFI_TRUSTED_COUNT="$(dumpsys adb 2>/dev/null | awk 'index($0, "<wifiAP ") { n++ } END { print n + 0 }')"
awk '/^(MemAvailable|SwapTotal|SwapFree):/ { gsub(":", "", $1); print "MEM_" $1 "=" $2 "KiB" }' /proc/meminfo
awk '/^(pswpin|pswpout|pgmajfault|pgscan_kswapd|pgscan_direct|allocstall|compact_stall) / { print "VM_" $1 "=" $2 }' /proc/vmstat
for f in /sys/class/devfreq/bus-dmc/cur_freq /sys/class/devfreq/bus-leftbus/cur_freq /sys/class/devfreq/bus-display/cur_freq /sys/class/devfreq/13000000.gpu/cur_freq; do
    value="$(cat "$f" 2>/dev/null)"
    if [ -n "$value" ]; then
        echo "DEVFREQ_$f=$value"
    else
        echo "DEVFREQ_$f=UNAVAILABLE"
    fi
done
echo CPUFREQ_GOVERNOR="$(cat /sys/devices/system/cpu/cpufreq/policy0/scaling_governor 2>/dev/null)"
echo CPUFREQ_TRANSITIONS="$(cat /sys/devices/system/cpu/cpufreq/policy0/stats/total_trans 2>/dev/null)"
zram_compr="$(cat /sys/block/zram0/comp_algorithm 2>/dev/null)"
echo ZRAM_COMPR="${zram_compr:-UNAVAILABLE}"
logcat -d -b kernel -t 2000 -v brief 2>/dev/null | awk '
index($0,"SMT311_G582_OPP_REJECT") { opp++ }
index($0,"dwc2_hsotg_ep_stop_xfr: timeout") { dwc2++ }
index($0,"failed to get event from devfreq-event devices") { event++ }
index($0,"Kernel panic") || index($0,"BUG: unable to handle") || index($0,"Oops:") { fatal++ }
END {
    print "LOG_KERNEL_OPP_REJECT=" opp+0
    print "LOG_KERNEL_DWC2_STOP_TIMEOUT=" dwc2+0
    print "LOG_KERNEL_DEVFREQ_EVENT_FAIL=" event+0
    print "LOG_KERNEL_FATAL=" fatal+0
}'
logcat -d -b crash -t 500 -v brief 2>/dev/null | awk '
index($0,"Fatal signal") { fatal++ }
END { print "LOG_CRASH_FATAL_SIGNAL=" fatal+0 }'
echo G598_END
