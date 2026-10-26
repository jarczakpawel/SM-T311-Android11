#!/system/bin/sh
# G913: consumer BQ max+1 defer fix, no changes to queue depth or codecs.
# Test a full 12s MP4 BEFORE detaching native library. Never overwrite ROM.
set -eu
mode="${1:---trial}"
file=/system/lib/libstagefright_bufferqueue_helper.so
stage=/data/local/tmp/G913_libstagefright_bufferqueue_helper.so
flag=/data/local/tmp/G913_TRACE_ACTIVE
old=250cebcf07e401b29266e1ca8270c5cdd279ca25dcb83c960359ab22779fadc0
new=c93949af3ba9fa9a905fb6b02c9afd0ccfb27f32213dc3697e7952f39f3a4374
sha() { sha256sum "$1" | cut -d ' ' -f 1; }
bound() { grep -F " $file " /proc/self/mountinfo >/dev/null 2>&1; }
codec_restart() {
  previous="$(pidof media.codec 2>/dev/null || true)"
  setprop ctl.restart vendor.media.omx
  i=0
  while [ "$i" -lt 15 ]; do
    sleep 1
    next="$(pidof media.codec 2>/dev/null || true)"
    if [ -n "$next" ] && [ "$next" != "$previous" ] &&
       [ "$(getprop init.svc.vendor.media.omx)" = running ]; then
      echo "G913_CODEC_RESTARTED_PID=$next"
      return 0
    fi
    i=$((i + 1))
  done
  echo G913_CODEC_RESTART_FAILED
  return 30
}
restore() {
  if bound; then
    umount -l "$file" || { echo G913_DETACH_FAILED; return 31; }
  fi
  [ "$(sha "$file")" = "$old" ] || { echo G913_OLD_SHA_MISMATCH; return 32; }
  codec_restart || return $?
  rm -f "$flag"
  echo G913_ORIGINAL_LIB_RESTORED_AND_SERVICE_RESTARTED
}
case "$mode" in
  --watchdog)
    sleep 90
    if [ -e "$flag" ] || bound; then restore; else echo G913_WATCHDOG_IDLE; fi
    exit $?;;
  --restore) restore; exit $?;;
  --trial|--stress) ;;
  *) exit 2;;
esac
[ "$(id -u)" = 0 ] || exit 10
[ "$(getprop ro.product.device)" = lt013g ] || exit 11
[ "$(getprop ro.build.version.sdk)" = 30 ] || exit 12
[ "$(getprop sys.boot_completed)" = 1 ] || exit 13
[ "$(getenforce)" = Enforcing ] || exit 14
[ "$(getprop init.svc.vendor.media.omx)" = running ] || exit 15
[ "$(sha "$file")" = "$old" ] || exit 16
[ "$(sha "$stage")" = "$new" ] || exit 17
[ "$(stat -c '%a' "$stage")" = 644 ] || exit 18
ls -Z "$stage" | grep -F 'u:object_r:system_lib_file:s0' >/dev/null || exit 19
[ ! -e "$flag" ] || exit 20
bound && exit 21
if [ "$mode" = --stress ]; then
  record_file=/data/local/tmp/G913_stress30_record.mp4
  limit=30
else
  record_file=/data/local/tmp/G913_trace_record.mp4
  limit=12
fi
[ ! -e "$record_file" ] || exit 22
# Arm independent watchdog BEFORE the bind mount and process restart.
setsid sh "$0" --watchdog >/data/local/tmp/G913_watchdog.log 2>&1 </dev/null &
touch "$flag"
trap 'restore' EXIT
mount -o bind "$stage" "$file"
[ "$(sha "$file")" = "$new" ] || exit 23
codec_restart
started="$(date '+%m-%d %H:%M:%S')"
echo "G913_RECORD_START=$started"
am start -a android.settings.SETTINGS >/dev/null
screenrecord --size 640x1024 --bit-rate 6000000 --time-limit "$limit" \
    "$record_file" >/data/local/tmp/G913_recorder.log 2>&1 &
p=$!
sleep 2
input swipe 430 1000 410 320 950 >/dev/null
sleep 2
input swipe 400 940 425 300 950 >/dev/null
sleep 2
input swipe 420 900 410 300 950 >/dev/null
if [ "$mode" = --stress ]; then
  i=0
  while [ "$i" -lt 18 ]; do
    input swipe 420 930 410 310 460 >/dev/null
    i=$((i + 1))
  done
fi
wait "$p"
echo G913_CAPTURE_COMPLETE_BEFORE_ROLLBACK
ls -l "$record_file"
logcat -b main,system -d -t 4500 | grep -E 'G913_GBS_DEFERRED|G912_GBS_REJECTED' | tail -n 12
input keyevent KEYCODE_HOME
exit 0
