#!/system/bin/sh
# G1587: diagnostic only, display awake / Android 11 G1568 accepted SystemUI.
# No sysfs writes, props, service restarts, TWRP, backups, or audio.
set -eu
[ "$(getprop ro.product.device)" = lt013g ] || exit 21
[ "$(getprop ro.build.version.incremental)" = SMT311G1441 ] || exit 22
[ "$(getprop sys.boot_completed)" = 1 ] || exit 23
dumpsys power | awk '/mWakefulness=Awake/ {ok=1} END {exit !ok}' || exit 24
dumpsys display | awk '/mGlobalDisplayState=ON/ {ok=1} END {exit !ok}' || exit 25
dumpsys SurfaceFlinger --list | grep -qx 'NotificationShade#0' || exit 26
BASE=/data/local/tmp/G1587-SM-T311-HWC-HARDWARE-LAYERS
for name in HOME OPEN CLOSE SUMMARY; do
    [ ! -e "${BASE}-${name}.txt" ] || exit 27
done
capture_layer() {
    label="$1"
    dumpsys SurfaceFlinger |
       grep -E 'HWC missed frame count|Total frames count:|ValidateDisplay unexpected composition failures|ValidateDisplay normal plane-exhaustion|PresentDisplay composition errors|ImportBuffer failures|Planner failures|Submit/test failures|Composition efficiency:|Actual DRM atomic frame ioctl failures|SM-T311 one-atomic-Present mode:|Output Layer 0x|hwc: layer=' > "${BASE}-${label}.txt"
    [ -s "${BASE}-${label}.txt" ] || exit 28
}
# Ensure the final visible state is collapsed as in the original UI baselines.
input touchscreen swipe 400 5 400 1000 420
sleep 1
capture_layer HOME
input touchscreen swipe 400 1200 400 150 420
sleep 1
capture_layer OPEN
input touchscreen swipe 400 5 400 1000 420
sleep 1
capture_layer CLOSE
{
    printf 'device=%s\n' "$(getprop ro.product.device)"
    printf 'rom_build=%s\n' "$(getprop ro.build.version.incremental)"
    printf 'kernel=%s\n' "$(uname -r)"
    printf 'selinux=%s\n' "$(getenforce)"
    printf 'hwmixed=%s\n' "$(getprop persist.vendor.t311.hwc_mixed_primary)"
    printf 'singleatomic=%s\n' "$(getprop persist.vendor.t311.hwc_single_atomic)"
    printf 'composer_service=%s\n' "$(getprop init.svc.vendor.hwcomposer-2-1)"
    printf 'surfaceflinger_pid=%s\n' "$(pidof surfaceflinger)"
    printf 'lmkd=%s\n' "$(getprop init.svc.lmkd)"
} > "${BASE}-SUMMARY.txt"
cat "${BASE}-SUMMARY.txt"
printf 'G1587_PHYSICAL_FIMD_HWC_CLIENT_DEVICE_LAYER_SNAPSHOT_PASS\n'
