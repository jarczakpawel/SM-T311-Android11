#!/system/bin/sh
# G1127 one TEST-ONLY opt-in PowerHAL fix, exact G958 and G1012.
# Uses volatile bind, no flash or backup, no audio, no unapproved OPP/voltage.
# Restores service+rightbus sysfs on ALL exits; separate watchdog for ADB failure.
set -eu
self=/data/local/tmp/G1127_POWERHAL_TRIAL.sh
dir=/data/local/tmp/G1127_POWERHAL_H264_PRIVATE
candidate=/data/local/tmp/G1127_POWER_HAL_ARM32
target=/vendor/bin/hw/android.hardware.power@1.0-service.lt013g
service=vendor.power-hal-1-0
bus=/sys/class/devfreq/bus-rightbus
prop=debug.t311.mfc_rightbus_hold
old_sha=cebbb198b87586d65b0bfd2e48a82aee191546a16f64ddf2b4323683e8258a71
new_sha=caae29c7fd9f47d68428d5f39fa36b8d36683df0c86f939d1b50194fcabe0731
sha(){ sha256sum "$1" | cut -d ' ' -f1; }
safe_restore(){
 setprop "$prop" 0 || true
 if [ -w "$bus/min_freq" ]; then echo 100000000 > "$bus/min_freq" 2>/dev/null || true; fi
 found="$(sha "$target" 2>/dev/null || true)"
 if [ "$found" = "$new_sha" ]; then
   setprop ctl.stop "$service" || true
   sleep 1
   umount "$target" 2>/dev/null || umount /system/vendor/bin/hw/android.hardware.power@1.0-service.lt013g 2>/dev/null || true
 fi
 if [ "$(sha "$target" 2>/dev/null || true)" = "$old_sha" ]; then
   setprop ctl.start "$service" || true
 fi
 if [ -d "$dir" ]; then
   printf 'target_sha=%s min=%s service=%s\n' "$(sha "$target" 2>/dev/null)" "$(cat "$bus/min_freq" 2>/dev/null)" "$(getprop init.svc.$service)" >> "$dir/restore-status.txt"
 fi
}
if [ "${1:-}" = "--watchdog" ]; then
 sleep 120
 safe_restore
 exit 0
fi
[ "${1:-}" = "--trial" ] || exit 2
[ "$(id -u)" = 0 ] || exit 10
[ "$(getprop ro.product.device)" = lt013g ] || exit 11
[ "$(getprop ro.build.version.sdk)" = 30 ] || exit 12
[ "$(getprop sys.boot_completed)" = 1 ] || exit 13
[ "$(getenforce)" = Enforcing ] || exit 14
[ "$(sha /dev/block/mmcblk0p9)" = e42c1b09b4c4d3c91b1a3c5330619d37560287821994d269ba67a970069cd5fd ] || exit 15
[ "$(sha /dev/block/mmcblk0p10)" = f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f ] || exit 16
[ "$(sha /system/vendor/lib/libv4l2_codec2_components.so)" = f735ad9ed95bff53eb0f928476b61b27f9875e23465f3f196bfe5d24877dd7c9 ] || exit 17
[ "$(sha /system/vendor/lib/libril.so)" = 4e6bb8a9465005a01f5bebd92a7e0ad247c0c4b609809c2600547bc97999bcb7 ] || exit 18
[ "$(sha /system/bin/screenrecord)" = f9e3669acd0c9d75e4c0f209513c63d8c33867172a995f068a73f00261437634 ] || exit 19
[ "$(sha "$target")" = "$old_sha" ] || exit 20
[ "$(sha "$candidate")" = "$new_sha" ] || exit 21
[ "$(getprop init.svc.$service)" = running ] || exit 22
[ "$(settings get system sound_effects_enabled)" = 0 ] || exit 23
[ "$(settings get system lockscreen_sounds_enabled)" = 0 ] || exit 24
[ "$(cat "$bus/min_freq")" = 100000000 ] || exit 25
[ "$(cat "$bus/max_freq")" = 200000000 ] || exit 26
[ "$(getprop "$prop")" != 1 ] || exit 27
[ "$(df -k /data | awk 'NR==2 {print $4}')" -ge 110000 ] || exit 28
[ ! -e "$dir" ] || exit 29
mkdir -m 700 "$dir"
watchdog_pid=""
gesture_pid=""
monitor_pid=""
cleanup(){
 [ -z "$gesture_pid" ] || kill "$gesture_pid" 2>/dev/null || true
 [ -z "$monitor_pid" ] || kill "$monitor_pid" 2>/dev/null || true
 safe_restore
 [ -z "$watchdog_pid" ] || kill "$watchdog_pid" 2>/dev/null || true
}
trap cleanup EXIT
setsid sh "$self" --watchdog >/dev/null 2>&1 </dev/null &
watchdog_pid=$!
setprop ctl.stop "$service"
n=0
while [ "$n" -lt 15 ] && [ "$(getprop init.svc.$service)" != stopped ]; do
 sleep 1
 n=$((n+1))
done
[ "$(getprop init.svc.$service)" = stopped ] || exit 30
mount -o bind "$candidate" "$target" || exit 31
[ "$(sha "$target")" = "$new_sha" ] || exit 32
setprop "$prop" 1
setprop ctl.start "$service"
n=0
while [ "$n" -lt 15 ] && [ "$(getprop init.svc.$service)" != running ]; do
 sleep 1
 n=$((n+1))
done
[ "$(getprop init.svc.$service)" = running ] || exit 33
echo 200000000 > "$bus/min_freq"
[ "$(cat "$bus/min_freq")" = 200000000 ] || exit 34
# Prove that native PowerHAL now respects min200 during a Settings launch
# AND touch. G1126 lost it within ~2s of user-interaction activity.
am start -a android.settings.SETTINGS > "$dir/settings.log" 2>&1
sleep 3
printf 'after_launch_min=%s after_launch_cur=%s\n' "$(cat "$bus/min_freq")" "$(cat "$bus/cur_freq")" > "$dir/functional-check.txt"
[ "$(cat "$bus/min_freq")" = 200000000 ] && [ "$(cat "$bus/cur_freq")" = 200000000 ] || exit 35
input swipe 400 950 400 240 350 >/dev/null 2>&1 || true
sleep 2
printf 'after_swipe_min=%s after_swipe_cur=%s\n' "$(cat "$bus/min_freq")" "$(cat "$bus/cur_freq")" >> "$dir/functional-check.txt"
[ "$(cat "$bus/min_freq")" = 200000000 ] && [ "$(cat "$bus/cur_freq")" = 200000000 ] || exit 36
echo G1127_POWERHAL_PIN_FUNCTIONAL_PASS
cat "$dir/functional-check.txt"
printf 'uptime_s,rightbus_hz,mfc_hz,leftbus_hz,dmc_hz,rightbus_min_hz\n' > "$dir/freq-trace.csv"
(
 n=0
 while [ "$n" -lt 78 ]; do
  printf '%s,%s,%s,%s,%s,%s\n' "$(cut -d ' ' -f1 /proc/uptime)" \
    "$(cat "$bus/cur_freq")" \
    "$(cat /sys/class/devfreq/bus-mfc/cur_freq)" \
    "$(cat /sys/class/devfreq/bus-leftbus/cur_freq)" \
    "$(cat /sys/class/devfreq/bus-dmc/cur_freq)" \
    "$(cat "$bus/min_freq")"
  n=$((n+1))
  sleep 0.5
 done
) >> "$dir/freq-trace.csv" 2>"$dir/freq-stderr.txt" &
monitor_pid=$!
t0="$(cut -d ' ' -f1 /proc/uptime)"
/system/bin/screenrecord --size 640x1024 --bit-rate 6000000 --time-limit 52 "$dir/real.mp4" > "$dir/screenrecord.stderr" 2>&1 &
recpid=$!
(
 sleep 2
 n=0
 while [ "$n" -lt 88 ]; do
  if [ $((n % 2)) -eq 0 ]; then input swipe 400 1000 400 250 350 >/dev/null 2>&1 || true
  else input swipe 400 250 400 1000 350 >/dev/null 2>&1 || true
  fi
  n=$((n+1))
 done
) &
gesture_pid=$!
set +e
wait "$recpid"
rc=$?
set -e
t1="$(cut -d ' ' -f1 /proc/uptime)"
kill "$gesture_pid" 2>/dev/null || true
wait "$gesture_pid" 2>/dev/null || true
gesture_pid=""
kill "$monitor_pid" 2>/dev/null || true
wait "$monitor_pid" 2>/dev/null || true
monitor_pid=""
[ -s "$dir/real.mp4" ] || exit 37
{
 echo "G1127_screenrecord_exit=$rc"
 echo "G1127_start_uptime=$t0"
 echo "G1127_end_uptime=$t1"
 echo "G1127_video_sha256=$(sha "$dir/real.mp4")"
 echo "G1127_trace_sha256=$(sha "$dir/freq-trace.csv")"
 echo "G1127_min_after_capture=$(cat "$bus/min_freq")"
 echo "G1127_cur_after_capture=$(cat "$bus/cur_freq")"
 echo "G1127_hal_bin_sha256=$(sha "$target")"
} > "$dir/receipt.txt"
cat "$dir/receipt.txt"
[ "$rc" = 0 ] || exit 38
[ "$(cat "$bus/min_freq")" = 200000000 ] || exit 39
[ "$(cat "$bus/cur_freq")" = 200000000 ] || exit 40
echo G1127_LONG_PHYSICAL_H264_PIN_CONTROL_COMPLETE
