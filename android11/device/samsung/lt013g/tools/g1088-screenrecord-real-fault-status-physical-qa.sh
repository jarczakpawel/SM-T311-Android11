#!/system/bin/sh
# G1088: one-shot real hardware H264 screenrecord through LIVE G1086 corrected
# screenrecord; no audio, backup, flash, rootfs modification, codec replacement.
set -eu
video=/data/local/tmp/G1088_SCREENRECORD_REAL_PRIVATE.mp4
log=/data/local/tmp/G1088_SCREENRECORD_REAL_PRIVATE.log
result=/data/local/tmp/G1088_SCREENRECORD_RESULT_PRIVATE.txt
binary=/system/bin/screenrecord
sha() { sha256sum "$1" | cut -d' ' -f1; }
[ "$(id -u)" = 0 ] || exit 11
[ "$(getprop ro.product.device)" = lt013g ] || exit 12
[ "$(getprop sys.boot_completed)" = 1 ] || exit 13
[ "$(getenforce)" = Enforcing ] || exit 14
[ "$(settings get system sound_effects_enabled)" = 0 ] || exit 15
[ "$(settings get system lockscreen_sounds_enabled)" = 0 ] || exit 16
[ "$(sha "$binary")" = 4bdbac4413ce8a2a816139e09c1546750793eac94299a71d51861a551891f3d6 ] || exit 17
[ "$(sha /system/vendor/lib/libv4l2_codec2_components.so)" = f735ad9ed95bff53eb0f928476b61b27f9875e23465f3f196bfe5d24877dd7c9 ] || exit 18
[ "$(getprop init.svc.android-hardware-media-c2-v4l2-hal-1-0)" = running ] || exit 19
[ ! -e "$video" ] && [ ! -e "$result" ] || exit 20
echo "G1088_SOURCE_EXIT_QA_ONLY_STARTED" >"$result"
# Recording is video-only: screenrecord does not acquire microphone.
am start -a android.settings.SETTINGS >/dev/null 2>&1 || true
"$binary" --size 640x1024 --bit-rate 6000000 --time-limit 70 "$video" >"$log" 2>&1 &
pid=$!
i=0
while [ "$i" -lt 60 ] && kill -0 "$pid" 2>/dev/null; do
  if [ $((i % 2)) -eq 0 ]; then
    input swipe 380 885 375 250 280 >/dev/null 2>&1 || true
  else
    input swipe 375 250 380 885 280 >/dev/null 2>&1 || true
  fi
  i=$((i+1))
  sleep 1
done
if wait "$pid"; then rc=0; else rc=$?; fi
echo "G1088_REAL_SCREENRECORD_SHELL_RC=$rc" >>"$result"
echo "G1088_SWIPE_COUNT=$i" >>"$result"
if [ -f "$video" ]; then
  echo "G1088_VIDEO_BYTES=$(wc -c < "$video" | tr -d ' ')" >>"$result"
  echo "G1088_VIDEO_SHA256=$(sha "$video")" >>"$result"
else
  echo "G1088_NO_VIDEO" >>"$result"
fi
echo "G1088_STDERR_BYTES=$(wc -c < "$log" | tr -d ' ')" >>"$result"
echo "G1088_POST_CODEC_SHA=$(sha /system/vendor/lib/libv4l2_codec2_components.so)" >>"$result"
echo "G1088_POST_SCREENREC_SHA=$(sha "$binary")" >>"$result"
echo "G1088_SOUND=$(settings get system sound_effects_enabled),$(settings get system lockscreen_sounds_enabled)" >>"$result"
echo G1088_DONE >>"$result"
