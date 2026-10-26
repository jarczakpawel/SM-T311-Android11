#!/system/bin/sh
# G1131 three distinct H264 sessions with CPU/GPU max PM QoS: lifecycle, stress, no extra devices.
# Read only sysfs, no backup, no sound, no PIN, no kernel writes.
set -eu
base=/data/local/tmp/G1131_PEAK_QOS_THREE_RESTARTS_PRIVATE
enc=/sys/devices/platform/soc/13400000.codec/active_encoders
shasum(){ sha256sum "$1" | cut -d' ' -f1; }
[ "$(id -u)" = 0 ] || exit 10
[ "$(getprop ro.product.device)" = lt013g ] || exit 11
[ "$(getprop sys.boot_completed)" = 1 ] || exit 12
[ "$(getenforce)" = Enforcing ] || exit 13
[ "$(shasum /dev/block/mmcblk0p9)" = 478a9da10024c6ac6426b44ad779b0d6ae66c1355d9b09c2fbdd147ba8140d7f ] || exit 14
[ "$(shasum /system/vendor/lib/libv4l2_codec2_components.so)" = f735ad9ed95bff53eb0f928476b61b27f9875e23465f3f196bfe5d24877dd7c9 ] || exit 15
[ "$(shasum /system/bin/screenrecord)" = f9e3669acd0c9d75e4c0f209513c63d8c33867172a995f068a73f00261437634 ] || exit 16
[ "$(getprop init.svc.vendor.power-hal-1-0)" = running ] || exit 17
[ "$(settings get system sound_effects_enabled)" = 0 ] || exit 18
[ "$(settings get system lockscreen_sounds_enabled)" = 0 ] || exit 19
[ "$(cat "$enc")" = 0 ] || exit 20
[ ! -e "$base" ] || exit 21
mkdir -m 700 "$base"
printf 'index,screenrecord_rc,mp4_sha256,trace_sha256,encoder_after,dmc_after,rightbus_after\n' > "$base/receipt.csv"
gesture_pid=""
monitor_pid=""
cleanup(){
 [ -z "$gesture_pid" ] || kill "$gesture_pid" 2>/dev/null || true
 [ -z "$monitor_pid" ] || kill "$monitor_pid" 2>/dev/null || true
 am force-stop com.android.settings >/dev/null 2>&1 || true
}
trap cleanup EXIT
am start -a android.settings.SETTINGS > "$base/settings.log" 2>&1
sleep 1
i=0
while [ "$i" -lt 3 ]; do
 [ "$(cat "$enc")" = 0 ] || exit 22
 case "$(cat /sys/class/devfreq/bus-rightbus/min_freq)" in
 100000000|160000000|200000000) : ;;
 *) exit 23 ;;
 esac
 [ "$(cat /sys/class/thermal/thermal_zone1/temp)" -lt 65000 ] || exit 27
 printf 'uptime,encoder,rightbus_min,rightbus_cur,dmc_min,leftbus_min,cpu_min_khz,gpu_min_hz,cpu_temp_mC\n' > "$base/trace-$i.csv"
 (
  n=0
  while [ "$n" -lt 41 ]; do
   printf '%s,%s,%s,%s,%s,%s,%s,%s,%s\n' "$(cut -d' ' -f1 /proc/uptime)" "$(cat "$enc")" "$(cat /sys/class/devfreq/bus-rightbus/min_freq)" "$(cat /sys/class/devfreq/bus-rightbus/cur_freq)" "$(cat /sys/class/devfreq/bus-dmc/min_freq)" "$(cat /sys/class/devfreq/bus-leftbus/min_freq)" "$(cat /sys/devices/system/cpu/cpufreq/policy0/scaling_min_freq)" "$(cat /sys/class/devfreq/13000000.gpu/min_freq)" "$(cat /sys/class/thermal/thermal_zone1/temp)"
   n=$((n+1))
   sleep 0.35
  done
 ) >> "$base/trace-$i.csv" 2> "$base/trace-$i.err" &
 monitor_pid=$!
 /system/bin/screenrecord --size 640x1024 --bit-rate 6000000 --time-limit 18 "$base/clip-$i.mp4" > "$base/record-$i.err" 2>&1 &
 recpid=$!
 (
  n=0
  sleep 1
  while [ "$n" -lt 35 ]; do
   if [ $(( (n+i) % 2 )) -eq 0 ]; then
    input swipe 400 1000 400 250 310 >/dev/null 2>&1 || true
   else
    input swipe 400 250 400 1000 310 >/dev/null 2>&1 || true
   fi
   n=$((n+1))
  done
 ) &
 gesture_pid=$!
 set +e
 wait "$recpid"
 rec_rc=$?
 set -e
 kill "$gesture_pid" 2>/dev/null || true
 wait "$gesture_pid" 2>/dev/null || true
 gesture_pid=""
 kill "$monitor_pid" 2>/dev/null || true
 wait "$monitor_pid" 2>/dev/null || true
 monitor_pid=""
 sleep 2
 [ -s "$base/clip-$i.mp4" ] || exit 24
 active="$(cat "$enc")"
 dmc="$(cat /sys/class/devfreq/bus-dmc/min_freq)"
 right="$(cat /sys/class/devfreq/bus-rightbus/min_freq)"
 printf '%s,%s,%s,%s,%s,%s,%s\n' "$i" "$rec_rc" "$(shasum "$base/clip-$i.mp4")" "$(shasum "$base/trace-$i.csv")" "$active" "$dmc" "$right" >> "$base/receipt.csv"
 [ "$rec_rc" = 0 ] && [ "$active" = 0 ] || exit 25
 [ "$dmc" = 100000000 ] && [ "$right" = 100000000 ] || exit 26
 [ "$(cat /sys/devices/system/cpu/cpufreq/policy0/scaling_min_freq)" = 200000 ] || exit 28
 [ "$(cat /sys/class/devfreq/13000000.gpu/min_freq)" = 160000000 ] || exit 29
 i=$((i+1))
done
cat "$base/receipt.csv"
echo G1131_FOUR_DISTINCT_H264_SESSIONS_COMPLETE
