#!/system/bin/sh
# G912: read-only counters in rebuilt bqhelper, no producer/codec changes.
# Test a full 12s MP4 BEFORE detaching native library. Never overwrite ROM.
set -eu
mode="${1:---trial}"
file=/system/lib/libstagefright_bufferqueue_helper.so
stage=/data/local/tmp/G912_libstagefright_bufferqueue_helper.so
flag=/data/local/tmp/G912_TRACE_ACTIVE
old=250cebcf07e401b29266e1ca8270c5cdd279ca25dcb83c960359ab22779fadc0
new=cebdaca4fe2f7a9958b298515acdfb854a3c34d4f8660119df0a0bcf4102ada4
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
      echo "G912_CODEC_RESTARTED_PID=$next"
      return 0
    fi
    i=$((i + 1))
  done
  echo G912_CODEC_RESTART_FAILED
  return 30
}
restore() {
  if bound; then
    umount -l "$file" || { echo G912_DETACH_FAILED; return 31; }
  fi
  [ "$(sha "$file")" = "$old" ] || { echo G912_OLD_SHA_MISMATCH; return 32; }
  codec_restart || return $?
  rm -f "$flag"
  echo G912_ORIGINAL_LIB_RESTORED_AND_SERVICE_RESTARTED
}
case "$mode" in
  --watchdog)
    sleep 66
    if [ -e "$flag" ] || bound; then restore; else echo G912_WATCHDOG_IDLE; fi
    exit $?;;
  --restore) restore; exit $?;;
  --trial) ;;
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
[ ! -e /data/local/tmp/G912_trace_record.mp4 ] || exit 22
# Arm independent watchdog BEFORE the bind mount and process restart.
setsid sh "$0" --watchdog >/data/local/tmp/G912_watchdog.log 2>&1 </dev/null &
touch "$flag"
trap 'restore' EXIT
mount -o bind "$stage" "$file"
[ "$(sha "$file")" = "$new" ] || exit 23
codec_restart
started="$(date '+%m-%d %H:%M:%S')"
echo "G912_RECORD_START=$started"
am start -a android.settings.SETTINGS >/dev/null
screenrecord --size 640x1024 --bit-rate 6000000 --time-limit 12 \
    /data/local/tmp/G912_trace_record.mp4 >/data/local/tmp/G912_recorder.log 2>&1 &
p=$!
sleep 2
input swipe 430 1000 410 320 950 >/dev/null
sleep 2
input swipe 400 940 425 300 950 >/dev/null
sleep 2
input swipe 420 900 410 300 950 >/dev/null
wait "$p"
echo G912_CAPTURE_COMPLETE_BEFORE_ROLLBACK
ls -l /data/local/tmp/G912_trace_record.mp4
logcat -b main,system -d -t 4500 | grep 'G912_GBS_REJECTED' | tail -n 12
input keyevent KEYCODE_HOME
exit 0
