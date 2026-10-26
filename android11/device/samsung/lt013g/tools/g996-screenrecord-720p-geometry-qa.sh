#!/system/bin/sh
# G996: compare screenrecord 1280x720 geometry with physical successful 72s Camera2 video.
# No kernel, modem, Codec2, SELinux, SYSTEM partition or user preferences changed.
set -eu
VID=/data/local/tmp/G996_H264_720P_SCREEN_MOTION_PRIVATE.mp4
ERR=/data/local/tmp/G996_H264_720P_SCREEN_STDERR_PRIVATE.txt
FLAG=/data/local/tmp/G996_H264_QA_ACTIVE
PID=/data/local/tmp/G996_H264_QA_PID
BITRATE=6000000
DURATION=92
ACCEPTED=6137f22dad0e0d7670020d50fb86266c7755b24dd546cb7ce3830a2bb89f75f6
sha(){ sha256sum "$1" | cut -d' ' -f1; }
if [ "${1:-}" = "--watchdog" ]; then
 sleep 155
 if [ -e "$FLAG" ] && [ -r "$PID" ]; then
  p="$(cat "$PID")"
  cmd="$(tr '\000' ' ' </proc/"$p"/cmdline 2>/dev/null || true)"
  case "$cmd" in *screenrecord*G996_H264_720P_SCREEN_MOTION_PRIVATE.mp4*) kill -TERM "$p" 2>/dev/null || true;; esac
 fi
 exit 0
fi
[ "$(id -u)" = 0 ] || exit 10
[ "$(getprop ro.product.device)" = lt013g ] || exit 11
[ "$(getprop sys.boot_completed)" = 1 ] || exit 12
[ "$(getenforce)" = Enforcing ] || exit 13
[ "$(sha /system/vendor/lib/libv4l2_codec2_components.so)" = "$ACCEPTED" ] || exit 14
[ "$(getprop init.svc.android-hardware-media-c2-v4l2-hal-1-0)" = running ] || exit 15
[ ! -e "$VID" ] && [ ! -e "$FLAG" ] || exit 16
touch "$FLAG"
gesture=""
cleanup(){
 if [ -n "$gesture" ]; then
  kill "$gesture" 2>/dev/null || true
  wait "$gesture" 2>/dev/null || true
 fi
 rm -f "$FLAG" "$PID"
}
trap cleanup EXIT
setsid sh "$0" --watchdog >/data/local/tmp/G996_H264_QA_WATCHDOG_PRIVATE.log 2>&1 </dev/null &
am start -a android.settings.SETTINGS >/dev/null 2>&1
screenrecord --size 1280x720 --bit-rate "$BITRATE" --time-limit "$DURATION" "$VID" >"$ERR" 2>&1 &
recorder="$!"
echo "$recorder" >"$PID"
(
 sleep 2
 i=0
 while [ "$i" -lt 275 ]; do
  if [ $((i % 2)) -eq 0 ]; then
   input swipe 400 1010 400 275 390 >/dev/null 2>&1 || true
  else
   input swipe 400 275 400 1010 390 >/dev/null 2>&1 || true
  fi
  i=$((i+1))
 done
) &
gesture="$!"
wait "$recorder"
rc=$?
echo G996_RECORDER_RC="$rc"
[ -s "$VID" ] || { echo G996_NO_VIDEO; exit 20; }
sha "$VID"
echo G996_SCREEN_720P_92SEC_COMPLETE
exit "$rc"
