#!/system/bin/sh
# G982 - isolated 120-second detached compressed output trial, unchanged V4L2 queue.
# No backup, wipe, flash, SIM, RIL or permanent SYSTEM write; watchdog restores G975.
set -eu
mode="${1:---trial}"
target=/system/vendor/lib/libv4l2_codec2_components.so
stage=/data/local/tmp/G982_codec2_detached.so
flag=/data/local/tmp/G982_CODEC2_TRIAL_ACTIVE
result=/data/local/tmp/G982_codec2_trial.log
video=/data/local/tmp/G982_H264_MOTION_PRIVATE.mp4
old=6137f22dad0e0d7670020d50fb86266c7755b24dd546cb7ce3830a2bb89f75f6
new=816bceae0f577e96a3f7efebb1d0e923727b1249bc3469d6402187b033d18999
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
   echo "G982_HAL_RESTART_PASS_PID=$curr"
   return 0
  fi
  i=$((i+1))
 done
 echo 'G982_HAL_RESTART_TIMEOUT'
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
  umount "$target" || umount -l "$target" || { echo G982_UNMOUNT_FAILED; return 31; }
 fi
 [ "$(file_sha "$target")" = "$old" ] || { echo G982_G975_NOT_RESTORED; return 32; }
 setprop ctl.start "$svc"
 j=0
 while [ "$(getprop init.svc.$svc)" != running ] && [ "$j" -lt 18 ]; do
  sleep 1
  j=$((j+1))
 done
 [ "$(getprop init.svc.$svc)" = running ] || { echo G982_HAL_RESTORE_TIMEOUT; return 34; }
 rm -f "$flag"
 echo G982_G975_RESTORED
}
case "$mode" in
 --restore) restore; exit $? ;;
 --watchdog)
   sleep 220
   if [ -e "$flag" ] || bound; then restore >>"$result" 2>&1; else echo G982_WATCHDOG_INERT >>"$result"; fi
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
[ "$(file_sha "$target")" = "$old" ] || exit 16
[ "$(file_sha "$stage")" = "$new" ] || exit 17
[ "$(stat -c '%a' "$stage")" = 644 ] || exit 18
ls -Z "$stage" | grep -F 'u:object_r:vendor_file:s0' >/dev/null || exit 19
[ ! -e "$flag" ] || exit 20
bound && exit 21
[ ! -e "$video" ] || exit 22
setsid sh "$0" --watchdog >/data/local/tmp/G982_codec2_watchdog.log 2>&1 </dev/null &
touch "$flag"
trap 'restore' EXIT
mount -o bind "$stage" "$target"
[ "$(file_sha "$target")" = "$new" ] || exit 23
restart_hal
echo G982_DETACHED_COPY_TRIAL_ACTIVE
am start -a android.settings.SETTINGS >/dev/null 2>&1
screenrecord --size 640x1024 --bit-rate 6000000 --time-limit 120 "$video" >/data/local/tmp/G982_recorder_stderr.log 2>&1 &
pid=$!
sleep 2
i=0
while [ "$i" -lt 115 ]; do
 input swipe 400 1020 400 245 450 >/dev/null 2>&1 || true
 i=$((i+1))
done
wait "$pid"
rc=$?
echo "G982_SCREENRECORD_EXIT=$rc"
ls -l "$video"
[ -s "$video" ] || { echo G982_EMPTY_MP4; exit 24; }
logcat -b main -d -v brief | grep -Ei 'H.264 encoder profile=|Failed setting H.264|V4L2EncodeComponent|s5p.mfc' | tail -55 || true
input keyevent KEYCODE_HOME >/dev/null 2>&1 || true
exit "$rc"
