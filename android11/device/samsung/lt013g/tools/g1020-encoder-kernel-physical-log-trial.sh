#!/system/bin/sh
set -eu
sha() { sha256sum "$1" | cut -d ' ' -f1; }
video=/data/local/tmp/G1020_ENCODER_SHORT_PRIVATE.mp4
trace=/data/local/tmp/G1020_MFC_KERNEL_PRIVATE.txt
run=/data/local/tmp/G1020_SHORT_RECORDER_RESULT_PRIVATE.txt
[ "$(id -u)" = 0 ] || exit 10
[ "$(getprop ro.product.device)" = lt013g ] || exit 11
[ "$(getprop sys.boot_completed)" = 1 ] || exit 12
[ "$(getenforce)" = Enforcing ] || exit 13
[ "$(sha /dev/block/mmcblk0p9)" = 2e55b36318a0e90f3ee8e17b7e3b7c5f3fa453a223a4f5640f678817306a64b0 ] || exit 14
[ "$(sha /dev/block/mmcblk0p10)" = f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f ] || exit 15
[ "$(sha /system/vendor/lib/libv4l2_codec2_components.so)" = f735ad9ed95bff53eb0f928476b61b27f9875e23465f3f196bfe5d24877dd7c9 ] || exit 16
[ "$(sha /system/vendor/lib/libril.so)" = 4e6bb8a9465005a01f5bebd92a7e0ad247c0c4b609809c2600547bc97999bcb7 ] || exit 17
[ "$(getprop init.svc.android-hardware-media-c2-v4l2-hal-1-0)" = running ] || exit 18
[ ! -e "$video" ] || exit 19
[ ! -e "$trace" ] || exit 20
wm dismiss-keyguard
input keyevent KEYCODE_HOME
am start -a android.settings.SETTINGS >/dev/null 2>&1 || exit 21
screenrecord --size 640x1024 --bit-rate 6000000 --time-limit 35 "$video" >/data/local/tmp/G1020_SCREENRECORD_STDERR_PRIVATE.txt 2>&1 &
pid=$!
(
 i=0
 while [ "$i" -lt 85 ]; do
  if [ $((i % 2)) -eq 0 ]; then input swipe 400 1000 400 300 370 >/dev/null 2>&1 || true
  else input swipe 400 300 400 1000 370 >/dev/null 2>&1 || true
  fi
  i=$((i+1))
 done
) &
swipe_pid=$!
set +e
wait "$pid"
rc=$?
set -e
kill "$swipe_pid" 2>/dev/null || true
wait "$swipe_pid" 2>/dev/null || true
dmesg >"$trace"
echo "G1020_SCREENRECORD_RC=$rc" >"$run"
echo "G1020_KERNEL_TRACE_COUNT=$(grep -c G1020_MFC_ENC_FRAME "$trace" || true)" >>"$run"
sha256sum "$video" >>"$run"
ls -l "$video" >>"$run"
[ -s "$video" ] || exit 22
input keyevent KEYCODE_HOME >/dev/null 2>&1 || true
cat "$run"
exit "$rc"
