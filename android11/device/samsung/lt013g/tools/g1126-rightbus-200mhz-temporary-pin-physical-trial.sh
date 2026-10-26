#!/system/bin/sh
# G1126: one reversible, manufacturer-supported RIGHTBUS max OPP (200MHz)
# during real Codec2 Settings recording. Zero custom kernel/voltage/firmware,
# no modem/audio, no backups. All other devfreq devices unchanged.
set -eu
self=/data/local/tmp/G1126_RIGHTBUS_PIN_TRIAL.sh
dir=/data/local/tmp/G1126_RIGHTBUS_PIN_PRIVATE
bus=/sys/class/devfreq/bus-rightbus
sha(){ sha256sum "$1" | cut -d ' ' -f1; }
if [ "${1:-}" = "--watchdog" ]; then
 sleep 112
 if [ -r "$dir/original_min_freq" ] && [ -w "$bus/min_freq" ]; then
   old="$(cat "$dir/original_min_freq")"
   case "$old" in 100000000|134000000|160000000|200000000|0)
     echo "$old" > "$bus/min_freq" 2>/dev/null || true
     echo G1126_WATCHDOG_RESTORED >> "$dir/restore-log" ;;
   esac
 fi
 exit 0
fi
[ "${1:-}" = "--trial" ] || exit 2
[ "$(id -u)" = 0 ] || exit 10
[ "$(getprop ro.product.device)" = lt013g ] || exit 11
[ "$(getprop ro.build.version.sdk)" = 30 ] || exit 12
[ "$(getprop sys.boot_completed)" = 1 ] || exit 13
[ "$(uname -r)" = 7.3.0-pjarczak ] || exit 14
[ "$(getenforce)" = Enforcing ] || exit 15
[ "$(sha /dev/block/mmcblk0p9)" = e42c1b09b4c4d3c91b1a3c5330619d37560287821994d269ba67a970069cd5fd ] || exit 16
[ "$(sha /dev/block/mmcblk0p10)" = f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f ] || exit 17
[ "$(sha /system/vendor/lib/libv4l2_codec2_components.so)" = f735ad9ed95bff53eb0f928476b61b27f9875e23465f3f196bfe5d24877dd7c9 ] || exit 18
[ "$(sha /system/vendor/lib/libril.so)" = 4e6bb8a9465005a01f5bebd92a7e0ad247c0c4b609809c2600547bc97999bcb7 ] || exit 19
[ "$(sha /system/bin/screenrecord)" = f9e3669acd0c9d75e4c0f209513c63d8c33867172a995f068a73f00261437634 ] || exit 20
[ "$(getprop init.svc.android-hardware-media-c2-v4l2-hal-1-0)" = running ] || exit 21
[ "$(settings get system sound_effects_enabled)" = 0 ] || exit 22
[ "$(settings get system lockscreen_sounds_enabled)" = 0 ] || exit 23
[ "$(cat /sys/module/s5p_mfc/parameters/debug)" = 0 ] || exit 24
[ "$(cat "$bus/max_freq")" = 200000000 ] || exit 25
[ "$(cat "$bus/min_freq")" = 100000000 ] || exit 26
[ -w "$bus/min_freq" ] || exit 27
[ "$(df -k /data | awk 'NR==2 {print $4}')" -ge 100000 ] || exit 28
[ ! -e "$dir" ] || exit 29
mkdir -m 700 "$dir"
cat "$bus/min_freq" > "$dir/original_min_freq"
cat "$bus/available_frequencies" > "$dir/available_freq"
cat "$bus/trans_stat" > "$dir/before-trans_stat.txt"
gesture_pid=""
watchdog_pid=""
finished=0
restore(){
 old="$(cat "$dir/original_min_freq")"
 [ ! -w "$bus/min_freq" ] || echo "$old" > "$bus/min_freq" 2>/dev/null || true
 printf 'normal_min=%s current_min=%s current_freq=%s\n' "$old" "$(cat "$bus/min_freq" 2>/dev/null)" "$(cat "$bus/cur_freq" 2>/dev/null)" >> "$dir/restore-log"
 [ -z "$gesture_pid" ] || kill "$gesture_pid" 2>/dev/null || true
 [ -z "$watchdog_pid" ] || kill "$watchdog_pid" 2>/dev/null || true
}
trap restore EXIT
setsid sh "$self" --watchdog >/dev/null 2>&1 </dev/null &
watchdog_pid=$!
echo 200000000 > "$bus/min_freq"
[ "$(cat "$bus/min_freq")" = 200000000 ] || exit 30
sleep 1
[ "$(cat "$bus/cur_freq")" = 200000000 ] || exit 31
echo "G1126_PIN_START current=$(cat "$bus/cur_freq") max=$(cat "$bus/max_freq") min=$(cat "$bus/min_freq")"
# Snapshot enough samples to independently prove the pin truly held.
(
 n=0
 while [ "$n" -lt 65 ]; do
  printf '%s,%s,%s,%s,%s\n' "$(cut -d ' ' -f1 /proc/uptime)" \
    "$(cat /sys/class/devfreq/bus-rightbus/cur_freq)" \
    "$(cat /sys/class/devfreq/bus-mfc/cur_freq)" \
    "$(cat /sys/class/devfreq/bus-leftbus/cur_freq)" \
    "$(cat /sys/class/devfreq/bus-dmc/cur_freq)"
  n=$((n+1))
  sleep 0.5
 done
) > "$dir/freq-trace.csv" 2>"$dir/freq-trace.stderr" &
monitor_pid=$!
am start -a android.settings.SETTINGS > "$dir/settings.log" 2>&1
sleep 1
t0="$(cut -d ' ' -f1 /proc/uptime)"
/system/bin/screenrecord --size 640x1024 --bit-rate 6000000 --time-limit 52 "$dir/real.mp4" > "$dir/record.stderr" 2>&1 &
rec_pid=$!
(
 sleep 2
 n=0
 while [ "$n" -lt 92 ]; do
  if [ $((n % 2)) -eq 0 ]; then
    input swipe 400 1000 400 250 350 >/dev/null 2>&1 || true
  else
    input swipe 400 250 400 1000 350 >/dev/null 2>&1 || true
  fi
  n=$((n+1))
 done
) &
gesture_pid=$!
set +e
wait "$rec_pid"
rc=$?
set -e
t1="$(cut -d ' ' -f1 /proc/uptime)"
kill "$gesture_pid" 2>/dev/null || true
wait "$gesture_pid" 2>/dev/null || true
gesture_pid=""
kill "$monitor_pid" 2>/dev/null || true
wait "$monitor_pid" 2>/dev/null || true
cat "$bus/trans_stat" > "$dir/after-trans_stat.txt"
[ -s "$dir/real.mp4" ] || exit 32
{
 echo "G1126_record_rc=$rc"
 echo "G1126_record_start_uptime=$t0"
 echo "G1126_record_end_uptime=$t1"
 echo "G1126_video_sha=$(sha "$dir/real.mp4")"
 echo "G1126_trace_sha=$(sha "$dir/freq-trace.csv")"
 echo "G1126_pin_before_restore=$(cat "$bus/min_freq")"
 echo "G1126_current_before_restore=$(cat "$bus/cur_freq")"
} > "$dir/receipt.txt"
echo "$(cat "$bus/min_freq")" > "$dir/pre_restore_min.txt"
[ "$(cat "$bus/min_freq")" = 200000000 ] || exit 33
[ "$(cat "$bus/cur_freq")" = 200000000 ] || exit 34
[ "$rc" = 0 ] || exit 35
echo G1126_PHYSICAL_ONE_PIN_RECORD_COMPLETE
