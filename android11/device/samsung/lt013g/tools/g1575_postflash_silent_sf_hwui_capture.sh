#!/system/bin/sh
# G1575: read-only hardware display cadence and HWUI frame-stage comparison.
# Clears ONLY diagnostic counters; no properties, clocks, writes to partitions.
set -eu
[ "$(getprop ro.product.device)" = lt013g ] || exit 21
[ "$(getprop ro.build.version.incremental)" = SMT311G1441 ] || exit 22
[ "$(getprop sys.boot_completed)" = 1 ] || exit 23
dumpsys power | awk '/mWakefulness=Awake/ {ok=1} END {exit !ok}' || exit 24
dumpsys display | awk '/mGlobalDisplayState=ON/ {ok=1} END {exit !ok}' || exit 25
dumpsys SurfaceFlinger --list | grep -qx 'NotificationShade#0' || exit 26
BASE=/data/local/tmp/G1575-SM-T311-SHADE-CADENCE
for n in SF-LATENCY.txt HWUI-FRAMESTATS.txt SUMMARY.txt; do
    [ ! -e "${BASE}-${n}" ] || exit 27
done

# The historical baseline G1454/G1535 is an identical four-cycle silent swipe workload.
dumpsys SurfaceFlinger --latency-clear 'NotificationShade#0' >/dev/null
input touchscreen swipe 400 5 400 1000 420
sleep 1
dumpsys gfxinfo com.android.systemui reset >/dev/null
for i in 1 2 3 4; do
    input touchscreen swipe 400 1200 400 150 420
    sleep 1
    input touchscreen swipe 400 5 400 1000 420
    sleep 1
done
dumpsys SurfaceFlinger --latency 'NotificationShade#0' > "${BASE}-SF-LATENCY.txt"
dumpsys gfxinfo com.android.systemui framestats > "${BASE}-HWUI-FRAMESTATS.txt"
{
    printf 'device=%s\n' "$(getprop ro.product.device)"
    printf 'build=%s\n' "$(getprop ro.build.version.incremental)"
    printf 'kernel=%s\n' "$(uname -r)"
    printf 'systemui=%s\n' "$(pidof com.android.systemui)"
    printf 'selinux=%s\n' "$(getenforce)"
    printf 'lmkd=%s\n' "$(getprop init.svc.lmkd)"
    printf 'refresh_period_ns=%s\n' "$(head -n1 "${BASE}-SF-LATENCY.txt")"
    dumpsys gfxinfo com.android.systemui | grep -E '^Total frames rendered:|^Janky frames:|^50th percentile:|^90th percentile:|^50th gpu percentile:|^90th gpu percentile:' | head -n 6
} > "${BASE}-SUMMARY.txt"
cat "${BASE}-SUMMARY.txt"
printf 'G1575_CAPTURE_FINISHED_NO_AUDIO_NO_ROM_FLASH_NO_BACKUP_NO_REBOOT\n'
