#!/system/bin/sh
# G1091 - 24-second hardware MFCv5 input FD route + QBUF offsets (read-only ALOGI) diagnosis, no audio experiment, do not flash.
# No backup, wipe, flash, SIM, RIL or permanent SYSTEM write; watchdog restores G1012.
# Silent ONLY: reject if Android touch/lock effects are enabled, no audio playback.
# This test uses the real hardware MFCv5 encoder (not software AVC).
set -eu
mode="${1:---trial}"
target=/system/vendor/lib/libv4l2_codec2_components.so
stage=/data/local/tmp/G1091_qbuf_route_observer.so
flag=/data/local/tmp/G1091_CODEC2_TRIAL_ACTIVE
result=/data/local/tmp/G1091_CODEC2_TRIAL_PRIVATE.txt
video=/data/local/tmp/G1091_QBUF_ROUTE_PRIVATE.mp4
old=f735ad9ed95bff53eb0f928476b61b27f9875e23465f3f196bfe5d24877dd7c9
new=7fcb3c70fcebcb9b54c29bf72978dc94fbea26818fc949e6c81d018755065ef1
svc=android-hardware-media-c2-v4l2-hal-1-0
file_sha() { sha256sum "$1" | cut -d ' ' -f 1; }
bound() { awk -v f="$target" '$5==f { found=1; exit } END{exit !found}' /proc/self/mountinfo; }
restart_hal() {
 prev="$(getprop init.svc_debug_pid.$svc)"
 setprop ctl.restart "$svc"
 i=0
 while [ "$i" -lt 18 ]; do
  sleep 1
  curr="$(getprop init.svc_debug_pid.$svc)"
  if [ -n "$curr" ] && [ "$curr" != "$prev" ] && [ "$(getprop init.svc.$svc)" = running ]; then
   echo "G1091_HAL_RESTART_PASS_PID=$curr"
   return 0
  fi
  i=$((i+1))
 done
 echo 'G1091_HAL_RESTART_TIMEOUT'
 return 33
}
restore() {
 if bound; then
  # The running codec HAL dlopen()s this .so. Stop it before unmount.
  setprop ctl.stop "$svc"
  j=0
  while [ "$(getprop init.svc.$svc)" != stopped ] && [ "$j" -lt 12 ]; do
   sleep 1
   j=$((j+1))
  done
  umount "$target" || { echo G1091_UNMOUNT_FAILED; return 31; }
 fi
 [ "$(file_sha "$target")" = "$old" ] || { echo G1091_G1012_NOT_RESTORED; return 32; }
 setprop ctl.start "$svc"
 j=0
 while [ "$(getprop init.svc.$svc)" != running ] && [ "$j" -lt 18 ]; do
  sleep 1
  j=$((j+1))
 done
 [ "$(getprop init.svc.$svc)" = running ] || { echo G1091_HAL_RESTORE_TIMEOUT; return 34; }
 if [ -r /data/local/tmp/G1091_LOG_PID_PRIVATE ]; then
  kill "$(cat /data/local/tmp/G1091_LOG_PID_PRIVATE)" 2>/dev/null || true
  rm -f /data/local/tmp/G1091_LOG_PID_PRIVATE
 fi
 rm -f "$flag"
 [ "$(file_sha /system/vendor/lib/libril.so)" = 4e6bb8a9465005a01f5bebd92a7e0ad247c0c4b609809c2600547bc97999bcb7 ] || return 38
rm -f "$stage"
echo G1091_G1012_RESTORED
}
case "$mode" in
 --restore) restore; exit $? ;;
 --watchdog)
   sleep 120
   if [ -e "$flag" ] || bound; then restore >>"$result" 2>&1; else echo G1091_WATCHDOG_INERT >>"$result"; fi
   exit $?;;
 --trial) ;;
 *) exit 2;;
esac
[ "$(id -u)" = 0 ] || exit 10
[ "$(getprop ro.product.device)" = lt013g ] || exit 11
[ "$(getprop ro.build.version.sdk)" = 30 ] || exit 12
[ "$(getprop sys.boot_completed)" = 1 ] || exit 13
[ "$(getenforce)" = Enforcing ] || exit 14
[ "$(getprop init.svc.$svc)" = running ] || exit 15
[ "$(file_sha /system/vendor/lib/libril.so)" = 4e6bb8a9465005a01f5bebd92a7e0ad247c0c4b609809c2600547bc97999bcb7 ] || exit 35
[ "$(file_sha /dev/block/mmcblk0p9)" = e42c1b09b4c4d3c91b1a3c5330619d37560287821994d269ba67a970069cd5fd ] || exit 36
[ "$(file_sha /dev/block/mmcblk0p10)" = f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f ] || exit 37
[ "$(file_sha "$target")" = "$old" ] || exit 16
[ "$(file_sha "$stage")" = "$new" ] || exit 17
[ "$(stat -c '%a' "$stage")" = 644 ] || exit 18
ls -Z "$stage" | grep -F 'u:object_r:vendor_file:s0' >/dev/null || exit 19
[ ! -e "$flag" ] || exit 20
bound && exit 21
[ ! -e "$video" ] || exit 22
[ "$(settings get system sound_effects_enabled)" = 0 ] || { echo G1091_REFUSE_TOUCH_SOUNDS_NOT_MUTED; exit 40; }
[ "$(settings get system lockscreen_sounds_enabled)" = 0 ] || { echo G1091_REFUSE_LOCK_SOUNDS_NOT_MUTED; exit 41; }
wm dismiss-keyguard
input keyevent KEYCODE_HOME
dumpsys window | grep -F 'isKeyguardShowing=false' >/dev/null || exit 39
setsid sh "$0" --watchdog >/data/local/tmp/G1091_CODEC2_WATCHDOG_PRIVATE.txt 2>&1 </dev/null &
touch "$flag"
gesture_pid=""
cleanup() {
 if [ -n "$gesture_pid" ]; then
  kill "$gesture_pid" 2>/dev/null || true
  wait "$gesture_pid" 2>/dev/null || true
 fi
 restore
}
trap 'cleanup' EXIT
mount -o bind "$stage" "$target"
[ "$(file_sha "$target")" = "$new" ] || exit 23
restart_hal
echo G1091_MFC_HW_QBUF_ROUTE_OBSERVER_ACTIVE
logcat -b main -v brief -s V4L2EncodeComponent:I FormatConverter:I     >/data/local/tmp/G1091_QBUF_ROUTE_LOGCAT_PRIVATE.txt 2>&1 </dev/null &
echo "$!" >/data/local/tmp/G1091_LOG_PID_PRIVATE
am start -a android.settings.SETTINGS >/dev/null 2>&1
screenrecord --size 640x1024 --bit-rate 6000000 --time-limit 24 "$video" >/data/local/tmp/G1091_RECORDER_STDERR_PRIVATE.txt 2>&1 &
pid=$!
(
 sleep 2
 i=0
 while [ "$i" -lt 65 ]; do
  # Scroll both ways, do not sit on Settings' bottom edge for minutes.
  if [ $((i % 2)) -eq 0 ]; then
    input swipe 400 1010 400 275 390 >/dev/null 2>&1 || true
  else
    input swipe 400 275 400 1010 390 >/dev/null 2>&1 || true
  fi
  i=$((i+1))
 done
) &
gesture_pid=$!
# Always join the RECORDER, not the gesture generator: otherwise the
# recording can end while stale scripted input keeps the trial mounted.
wait "$pid"
rc=$?
kill "$gesture_pid" 2>/dev/null || true
wait "$gesture_pid" 2>/dev/null || true
gesture_pid=""
echo "G1091_SCREENRECORD_EXIT=$rc"
ls -l "$video"
[ -s "$video" ] || { echo G1091_EMPTY_MP4; exit 24; }
logcat -b main -d -v brief | grep -Ei 'G1091_|H.264 encoder profile=|Failed setting H.264|V4L2EncodeComponent|FormatConverter|s5p.mfc' | tail -55 || true
input keyevent KEYCODE_HOME >/dev/null 2>&1 || true
exit "$rc"
