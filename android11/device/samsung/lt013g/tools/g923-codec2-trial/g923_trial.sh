#!/system/bin/sh
set -eu
mode="${1:---trial}"
file=/system/vendor/lib/libv4l2_codec2_components.so
stage=/data/local/tmp/G923_codec2_output.so
flag=/data/local/tmp/G923_CAPTURE_TRIAL_ACTIVE
old=bd0a0b652c2835025121714b2683a78728d992ae9615ecb63c8037453b56de87
new=3964742af2c60360753c141d0fac3ce1b449e35deed3a8f27b3283cc8ae7e2b4
svc=android-hardware-media-c2-v4l2-hal-1-0
sha() { sha256sum "$1" | cut -d ' ' -f 1; }
bound() { awk -v target="$file" '$5 == target { found=1; exit } END { exit !found }' /proc/self/mountinfo; }
restart_hal() {
 oldpid="$(getprop init.svc_debug_pid.$svc)"
 setprop ctl.restart "$svc"
 i=0
 while [ "$i" -lt 15 ]; do
  sleep 1
  p="$(getprop init.svc_debug_pid.$svc)"
  if [ -n "$p" ] && [ "$p" != "$oldpid" ] && [ "$(getprop init.svc.$svc)" = running ]; then
   echo "G923_HAL_RUNNING_PID=$p"
   return 0
  fi
  i=$((i+1))
 done
 echo G923_HAL_RESTART_TIMEOUT; return 30
}
restore() {
 if bound;then umount -l "$file" || { echo G923_UNMOUNT_FAILED;return 31;};fi
 [ "$(sha "$file")" = "$old" ] || { echo G923_OLD_CODEC2_SHA_NOT_VISIBLE;return 32; }
 restart_hal || return $?
 rm -f "$flag"
 echo G923_ORIGINAL_CODEC2_RESTORED
}
case "$mode" in
 --watchdog)
  sleep 80
  if [ -e "$flag" ] || bound;then restore;else echo G923_WATCHDOG_NO_ACTION;fi
  exit $?;;
 --restore) restore;exit $?;;
 --trial) ;;
 *)echo G923_WRONG_MODE;exit 2;;
esac
[ "$(id -u)" = 0 ] || exit 10
[ "$(getprop ro.product.device)" = lt013g ] || exit 11
[ "$(getprop ro.build.version.sdk)" = 30 ] || exit 12
[ "$(getprop sys.boot_completed)" = 1 ] || exit 13
[ "$(getenforce)" = Enforcing ] || exit 14
[ "$(getprop init.svc.$svc)" = running ] || exit 15
[ "$(sha "$file")" = "$old" ] || exit 16
[ "$(sha "$stage")" = "$new" ] || exit 17
[ "$(stat -c '%a' "$stage")" = 644 ] || exit 18
ls -Z "$stage" | grep -F 'u:object_r:vendor_file:s0' >/dev/null || exit 19
[ ! -e "$flag" ] || exit 20
bound && exit 21
[ ! -e /data/local/tmp/G923_mfc_output16.mp4 ] || exit 22
setsid sh "$0" --watchdog >/data/local/tmp/G923_watchdog.log 2>&1 </dev/null &
touch "$flag"
trap 'restore' EXIT
mount -o bind "$stage" "$file"
[ "$(sha "$file")" = "$new" ] || exit 23
restart_hal
start="$(date '+%m-%d %H:%M:%S')"
echo "G923_CAPTURE_START=$start"
am start -a android.settings.SETTINGS >/dev/null
screenrecord --size 640x1024 --bit-rate 6000000 --time-limit 16 \
  /data/local/tmp/G923_mfc_output16.mp4 >/data/local/tmp/G923_recorder.log 2>&1 &
pid=$!
sleep 2
i=0
while [ "$i" -lt 11 ];do
 input swipe 415 980 420 310 450 >/dev/null
 i=$((i+1))
done
wait "$pid"
echo G923_COMPLETE_16SEC_MP4_BEFORE_RESTORE
ls -l /data/local/tmp/G923_mfc_output16.mp4
logcat -b main,system -d -t 7500 | grep -E 'G923_MFC_CAPTURE_PAYLOAD|G923_INVALID_' | tail -n 20 || true
input keyevent KEYCODE_HOME
exit 0
