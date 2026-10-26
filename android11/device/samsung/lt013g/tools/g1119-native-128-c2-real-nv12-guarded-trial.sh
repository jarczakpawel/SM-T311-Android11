#!/system/bin/sh
# G1119 - SILENT/BOUNDED experimental FULL Codec2 MFCv5 bridge physical trial.
# SHA gated, nonpersistent property, volatile bind mount, guaranteed G1012 HAL restore.
# This never flashes BOOT/TWRP/ROM or plays audio or modifies SIM/RIL.
set -eu
mode="${1:---trial}"
target=/system/vendor/lib/libv4l2_codec2_components.so
stage=/data/local/tmp/G1119_CODEC2_NATIVE_MFCV5.so
self=/data/local/tmp/G1119_CODEC2_TRIAL.sh
flag=/data/local/tmp/G1119_CODEC2_TRIAL_ACTIVE
out=/data/local/tmp/G1119_MEMFD_128_CAPTURE_PRIVATE
svc=android-hardware-media-c2-v4l2-hal-1-0
original=f735ad9ed95bff53eb0f928476b61b27f9875e23465f3f196bfe5d24877dd7c9
candidate=5134b2a44658964d7018e57b1851dbc515e1d6f8c019b763ec0eb27400df0abe
sha(){ sha256sum "$1" | cut -d ' ' -f 1; }
bound(){ awk -v f="$target" '$5==f {v=1;exit} END{exit !v}' /proc/self/mountinfo; }
restart(){
  previous="$(getprop init.svc_debug_pid.$svc)"
  setprop ctl.restart "$svc"
  i=0
  while [ "$i" -lt 20 ]; do
     sleep 1
     current="$(getprop init.svc_debug_pid.$svc)"
     if [ -n "$current" ] && [ "$current" != "$previous" ] &&
        [ "$(getprop init.svc.$svc)" = running ]; then
        echo "G1119_HAL_RESTART_PASS_PID=$current"
        return 0
     fi
     i=$((i+1))
  done
  return 61
}
restore(){
  setprop debug.t311.mfcv5_native 0 || true
  setprop debug.t311.mfcv5_capture 0 || true
  if bound; then
    setprop ctl.stop "$svc"
    i=0
    while [ "$i" -lt 16 ] && [ "$(getprop init.svc.$svc)" != stopped ]; do sleep 1; i=$((i+1)); done
    umount "$target" || return 62
  fi
  [ "$(sha "$target")" = "$original" ] || return 63
  setprop ctl.start "$svc"
  i=0
  while [ "$i" -lt 22 ] && [ "$(getprop init.svc.$svc)" != running ]; do sleep 1; i=$((i+1)); done
  [ "$(getprop init.svc.$svc)" = running ] || return 64
  rm -f "$flag"
  [ "$(getprop debug.t311.mfcv5_native)" = 0 ] || return 65
  [ "$(sha /system/vendor/lib/libril.so)" = 4e6bb8a9465005a01f5bebd92a7e0ad247c0c4b609809c2600547bc97999bcb7 ] || return 66
  echo G1119_G1012_RESTORED
}
case "$mode" in
 --restore) restore; exit $? ;;
 --watchdog)
    sleep 165
    if [ -e "$flag" ] || bound; then
      restore >>"$out.watchdog.log" 2>&1
    fi
    exit $? ;;
 --trial) ;;
 *) exit 2 ;;
esac
[ "$(id -u)" = 0 ] || exit 10
[ "$(getprop ro.product.device)" = lt013g ] || exit 11
[ "$(getprop ro.build.version.sdk)" = 30 ] || exit 12
[ "$(getprop sys.boot_completed)" = 1 ] || exit 13
[ "$(getenforce)" = Enforcing ] || exit 14
[ "$(getprop init.svc.$svc)" = running ] || exit 15
[ "$(sha /dev/block/mmcblk0p9)" = e42c1b09b4c4d3c91b1a3c5330619d37560287821994d269ba67a970069cd5fd ] || exit 16
[ "$(sha /dev/block/mmcblk0p10)" = f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f ] || exit 17
[ "$(sha /system/vendor/lib/libril.so)" = 4e6bb8a9465005a01f5bebd92a7e0ad247c0c4b609809c2600547bc97999bcb7 ] || exit 18
[ "$(sha "$target")" = "$original" ] || exit 19
[ "$(sha "$stage")" = "$candidate" ] || exit 20
[ "$(stat -c '%a' "$stage")" = 644 ] || exit 21
ls -Z "$stage" | grep -F 'u:object_r:vendor_file:s0' >/dev/null || exit 22
[ "$(settings get system sound_effects_enabled)" = 0 ] || exit 23
[ "$(settings get system lockscreen_sounds_enabled)" = 0 ] || exit 24
[ "$(cat /sys/module/s5p_mfc/parameters/debug)" = 0 ] || exit 25
[ ! -e "$flag" ] || exit 26
bound && exit 27
[ ! -e "$out.mp4" ] && [ ! -e "$out.result" ] || exit 28
[ "$(df -k /data | awk 'NR==2 {print $4}')" -ge 200000 ] || exit 29
[ -r "$self" ] || exit 30
[ ! -e "$out.nv12" ] || exit 39
# Start watchdog first; it can recover independently if this shell is killed.
setsid sh "$self" --watchdog >/dev/null 2>&1 </dev/null &
watchdog_pid=$!
gesture_pid=""
cleanup(){
  if [ -n "$gesture_pid" ]; then kill "$gesture_pid" 2>/dev/null || true; fi
  restore
  kill "$watchdog_pid" 2>/dev/null || true
}
trap 'cleanup' EXIT
touch "$flag"
setprop debug.t311.mfcv5_capture 1
setprop debug.t311.mfcv5_native 1
[ "$(getprop debug.t311.mfcv5_native)" = 1 ] || exit 31
mount -o bind "$stage" "$target"
[ "$(sha "$target")" = "$candidate" ] || exit 32
restart
am start -a android.settings.SETTINGS >/dev/null 2>&1
screenrecord --size 640x1024 --bit-rate 6000000 --time-limit 15 "$out.mp4" >"$out.stderr" 2>&1 &
recpid=$!
(
  sleep 2
  n=0
  while [ "$n" -lt 30 ]; do
     if [ $((n % 2)) -eq 0 ]; then
       input swipe 400 1000 400 250 360 >/dev/null 2>&1 || true
     else
       input swipe 400 250 400 1000 360 >/dev/null 2>&1 || true
     fi
     n=$((n+1))
  done
) &
gesture_pid=$!
# Export private memfd *while recording is still active*, before C2
# destroys the component. Its fd lifetime after C2 stop is not guaranteed.
sleep 12
halpid="$(getprop init.svc_debug_pid.$svc)"
echo "G1119_LIVE_HAL_PID=$halpid" > "$out.fdinfo"
ls -l /proc/$halpid/fd >> "$out.fdinfo" 2>&1 || true
exported=0
for link in /proc/$halpid/fd/*; do
   name="$(readlink "$link" 2>/dev/null || true)"
   case "$name" in
     *memfd:G1119_NV12_PRIVATE*)
       if cat "$link" > "$out.nv12" && [ "$(stat -c %s "$out.nv12")" = 125831184 ]; then
           echo G1119_LIVE_MEMFD_EXPORT_PASS >> "$out.fdinfo"
           sha256sum "$out.nv12" >> "$out.fdinfo"
           ls -l "$out.nv12" >> "$out.fdinfo"
           exported=1
       else
           echo G1119_LIVE_MEMFD_EXPORT_FAILED >> "$out.fdinfo"
       fi
       break ;;
   esac
done
[ "$exported" -eq 1 ] || echo G1119_LIVE_MEMFD_NOT_FOUND >> "$out.fdinfo"
set +e
wait "$recpid"
rc=$?
set -e
kill "$gesture_pid" 2>/dev/null || true
wait "$gesture_pid" 2>/dev/null || true
gesture_pid=""
{
 echo G1119_RECORD_EXIT=$rc
 echo G1119_MEMFD_EARLY_EXPORT_STATUS=$exported
 cat "$out.fdinfo"
 if [ -e "$out.nv12" ]; then sha256sum "$out.nv12"; fi
 if [ -e "$out.mp4" ]; then sha256sum "$out.mp4"; ls -l "$out.mp4"; fi
 logcat -b main -d -v brief | grep -E 'G1119_|G1116_|V4L2EncodeComponent|Codec2' | tail -n 80 || true
} >"$out.result"
cat "$out.result"
[ "$rc" -eq 0 ] && [ -s "$out.mp4" ]
