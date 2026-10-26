#!/system/bin/sh
# G1071: hardware-only Samsung MFCv5 900-frame V4L2 H264 with paced ~33ms new-frame QBUF.
# Compare same deterministic NV12 synthetic workload with independent G1048
# strict full-900 FFmpeg PASS. NO software encoding, camera, mic, audio, backup,
# ROM, BOOT, recovery, RIL, PIN/PUK, or permanent system writes.
set -eu
elf=/data/local/tmp/G1071_MFC_PACED_ARM32
video=/data/local/tmp/G1071_HW_REALTIME_PACED_PRIVATE.h264
log=/data/local/tmp/G1071_HW_PACED_RUN_LOG_PRIVATE.txt
result=/data/local/tmp/G1071_HW_PACED_RESULT_PRIVATE.txt
sha(){ sha256sum "$1" | cut -d ' ' -f1; }
[ "$(id -u)" = 0 ] || exit 11
[ "$(getprop ro.product.device)" = lt013g ] || exit 12
[ "$(getprop sys.boot_completed)" = 1 ] || exit 13
[ "$(getenforce)" = Enforcing ] || exit 14
[ "$(sha /dev/block/mmcblk0p9)" = e42c1b09b4c4d3c91b1a3c5330619d37560287821994d269ba67a970069cd5fd ] || exit 15
[ "$(sha /dev/block/mmcblk0p10)" = f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f ] || exit 16
[ "$(sha /system/vendor/lib/libv4l2_codec2_components.so)" = f735ad9ed95bff53eb0f928476b61b27f9875e23465f3f196bfe5d24877dd7c9 ] || exit 17
[ "$(sha /system/vendor/lib/libril.so)" = 4e6bb8a9465005a01f5bebd92a7e0ad247c0c4b609809c2600547bc97999bcb7 ] || exit 18
[ "$(sha "$elf")" = 3943705299c4e9076c03f017465a9bd6814de4afa591fce66eca1c5a4d3d2a82 ] || exit 19
[ "$(cat /sys/class/video4linux/video7/name)" = s5p-mfc-enc ] || exit 20
[ "$(getprop init.svc.android-hardware-media-c2-v4l2-hal-1-0)" = running ] || exit 21
[ ! -e "$video" ] && [ ! -e "$log" ] && [ ! -e "$result" ] || exit 22
[ "$(settings get system sound_effects_enabled)" = 0 ] || exit 23
[ "$(settings get system lockscreen_sounds_enabled)" = 0 ] || exit 24
[ "$(df -k /data | awk 'NR==2 {print $4}')" -ge 100000 ] || exit 25
# G1071 is headless: no UI input, wake, player, camera or display actions.
# Only the direct hardware encoder's raw OUTPUT QBUF cadence changes.
ulimit -f 65536
set +e
"$elf" --run >"$log" 2>&1
rc=$?
set -e
{
 echo "G1071_NATIVE_HARDWARE_EXIT=$rc"
 if [ -f "$video" ]; then sha256sum "$video"; ls -l "$video"; fi
 echo "G1071_MFC_DEVICE_NAME=$(cat /sys/class/video4linux/video7/name)"
 echo "G1071_SILENT_TOUCH=$(settings get system sound_effects_enabled)"
 tail -12 "$log"
} >"$result"
cat "$result"
exit "$rc"
