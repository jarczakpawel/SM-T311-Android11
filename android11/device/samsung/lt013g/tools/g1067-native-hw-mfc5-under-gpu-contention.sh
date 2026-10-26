#!/system/bin/sh
# G1067: hardware-only Samsung MFCv5 900-frame V4L2 H264 under GPU/UI contention.
# Compare same deterministic NV12 synthetic workload with independent G1048
# strict full-900 FFmpeg PASS. NO software encoding, camera, mic, audio, backup,
# ROM, BOOT, recovery, RIL, PIN/PUK, or permanent system writes.
set -eu
elf=/data/local/tmp/G1067_HW_MFC_ARM32
video=/data/local/tmp/G1067_HW_CONCURRENT_GPU_PRIVATE.h264
log=/data/local/tmp/G1067_HW_GPU_RUN_LOG_PRIVATE.txt
result=/data/local/tmp/G1067_HW_GPU_RESULT_PRIVATE.txt
sha(){ sha256sum "$1" | cut -d ' ' -f1; }
[ "$(id -u)" = 0 ] || exit 11
[ "$(getprop ro.product.device)" = lt013g ] || exit 12
[ "$(getprop sys.boot_completed)" = 1 ] || exit 13
[ "$(getenforce)" = Enforcing ] || exit 14
[ "$(sha /dev/block/mmcblk0p9)" = e42c1b09b4c4d3c91b1a3c5330619d37560287821994d269ba67a970069cd5fd ] || exit 15
[ "$(sha /dev/block/mmcblk0p10)" = f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f ] || exit 16
[ "$(sha /system/vendor/lib/libv4l2_codec2_components.so)" = f735ad9ed95bff53eb0f928476b61b27f9875e23465f3f196bfe5d24877dd7c9 ] || exit 17
[ "$(sha /system/vendor/lib/libril.so)" = 4e6bb8a9465005a01f5bebd92a7e0ad247c0c4b609809c2600547bc97999bcb7 ] || exit 18
[ "$(sha "$elf")" = 000acb1b9479afeb2ceed571f7aa4e1bd8225e80bbdc25afae1efc5efd16172b ] || exit 19
[ "$(cat /sys/class/video4linux/video7/name)" = s5p-mfc-enc ] || exit 20
[ "$(getprop init.svc.android-hardware-media-c2-v4l2-hal-1-0)" = running ] || exit 21
[ ! -e "$video" ] && [ ! -e "$log" ] && [ ! -e "$result" ] || exit 22
[ "$(settings get system sound_effects_enabled)" = 0 ] || exit 23
[ "$(settings get system lockscreen_sounds_enabled)" = 0 ] || exit 24
[ "$(df -k /data | awk 'NR==2 {print $4}')" -ge 100000 ] || exit 25
# This test intentionally drives the GPU compositing interface while MFC runs.
# Touch/lock sounds are fail-closed disabled by preflight, no media player.
wm dismiss-keyguard
input keyevent KEYCODE_HOME
am start -a android.settings.SETTINGS >/dev/null 2>&1
swipe_pid=''
cleanup(){
  if [ -n "$swipe_pid" ]; then
    kill "$swipe_pid" 2>/dev/null || true
    wait "$swipe_pid" 2>/dev/null || true
  fi
  input keyevent KEYCODE_HOME >/dev/null 2>&1 || true
}
trap cleanup EXIT
(
 i=0
 while [ "$i" -lt 220 ]; do
   if [ $((i % 2)) -eq 0 ]; then
     input swipe 400 1000 400 275 260 >/dev/null 2>&1 || true
   else
     input swipe 400 275 400 1000 260 >/dev/null 2>&1 || true
   fi
   i=$((i+1))
 done
) &
swipe_pid=$!
sleep 2
ulimit -f 65536
set +e
"$elf" --run >"$log" 2>&1
rc=$?
set -e
{
 echo "G1067_NATIVE_HARDWARE_EXIT=$rc"
 if [ -f "$video" ]; then sha256sum "$video"; ls -l "$video"; fi
 echo "G1067_MFC_DEVICE_NAME=$(cat /sys/class/video4linux/video7/name)"
 echo "G1067_SILENT_TOUCH=$(settings get system sound_effects_enabled)"
 tail -12 "$log"
} >"$result"
cat "$result"
exit "$rc"
