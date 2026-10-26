#!/system/bin/sh
# G1116 - headless native MFCv5 backend integration, separate from C2.
# Synthetic frames only; NO audio/GUI/modem/BOOT/TWRP/backup/flash/reboot.
set -eu
bin=/data/local/tmp/G1116_MFCV5_SYNC_SMOKE_ARM32
base=/data/local/tmp/G1116_MFCV5_NATIVE_PRIVATE
sha(){ sha256sum "$1" | cut -d ' ' -f 1; }
[ "$(id -u)" = 0 ] || exit 10
[ "$(getprop ro.product.device)" = lt013g ] || exit 11
[ "$(getprop ro.build.version.sdk)" = 30 ] || exit 12
[ "$(getprop sys.boot_completed)" = 1 ] || exit 13
[ "$(getenforce)" = Enforcing ] || exit 14
[ "$(sha /dev/block/mmcblk0p9)" = e42c1b09b4c4d3c91b1a3c5330619d37560287821994d269ba67a970069cd5fd ] || exit 15
[ "$(sha /dev/block/mmcblk0p10)" = f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f ] || exit 16
[ "$(sha /system/vendor/lib/libril.so)" = 4e6bb8a9465005a01f5bebd92a7e0ad247c0c4b609809c2600547bc97999bcb7 ] || exit 17
[ "$(sha /system/vendor/lib/libv4l2_codec2_components.so)" = f735ad9ed95bff53eb0f928476b61b27f9875e23465f3f196bfe5d24877dd7c9 ] || exit 18
[ "$(getprop init.svc.android-hardware-media-c2-v4l2-hal-1-0)" = running ] || exit 19
[ "$(cat /sys/class/video4linux/video7/name)" = s5p-mfc-enc ] || exit 20
[ "$(settings get system sound_effects_enabled)" = 0 ] || exit 21
[ "$(settings get system lockscreen_sounds_enabled)" = 0 ] || exit 22
[ "$(cat /sys/module/s5p_mfc/parameters/debug)" = 0 ] || exit 23
[ "$(sha "$bin")" = b0ce4e02aa4e35ed371c04703cfd1021cae613f866380765d9e87b546e0fa3cc ] || exit 24
[ ! -e "$base.640.h264" ] && [ ! -e "$base.720.h264" ] || exit 25
[ ! -e "$base.640.log" ] && [ ! -e "$base.720.log" ] || exit 26
[ ! -e "$base.receipt" ] || exit 27
[ "$(df -k /data | awk 'NR==2 {print $4}')" -ge 100000 ] || exit 28
set +e
"$bin" 640x1024 120 "$base.640.h264" > "$base.640.log" 2>&1
r640=$?
if [ "$r640" -eq 0 ]; then
    "$bin" 1280x720 120 "$base.720.h264" > "$base.720.log" 2>&1
    r720=$?
else
    r720=90
fi
set -e
{
 echo "G1116_NATIVE_RESULT_640=$r640"
 echo "G1116_NATIVE_RESULT_720=$r720"
 for dim in 640 720; do
    if [ -f "$base.$dim.h264" ]; then
       sha256sum "$base.$dim.h264"
       ls -l "$base.$dim.h264"
    fi
    if [ -f "$base.$dim.log" ]; then
       sha256sum "$base.$dim.log"
       tail -n 8 "$base.$dim.log"
    fi
 done
 echo "G1116_POST_HARDWARE_SHA_C2=$(sha /system/vendor/lib/libv4l2_codec2_components.so)"
 echo "G1116_POST_SOUND_EFFECTS=$(settings get system sound_effects_enabled)"
 echo "G1116_POST_LOCK_SOUND=$(settings get system lockscreen_sounds_enabled)"
} > "$base.receipt"
cat "$base.receipt"
[ "$r640" -eq 0 ] && [ "$r720" -eq 0 ]
