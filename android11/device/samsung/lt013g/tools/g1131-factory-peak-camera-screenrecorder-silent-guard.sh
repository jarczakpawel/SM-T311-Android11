#!/system/bin/sh
# G1131 factory-peak CPU/GPU/DMC trial on real SM-T311. No sound or backups.
# Read-only sysfs telemetry; no writes to clocks, PowerHAL, thermal or SIM.
set -eu
d=/data/local/tmp/G1131_FACTORY_PEAK_FIRST_PRIVATE
m=/sys/devices/platform/soc/13400000.codec/active_encoders
sha(){ sha256sum "$1" | cut -d' ' -f1; }
[ "$(id -u)" = 0 ] || exit 10
[ "$(getprop ro.product.device)" = lt013g ] || exit 11
[ "$(getprop sys.boot_completed)" = 1 ] || exit 12
[ "$(getenforce)" = Enforcing ] || exit 13
[ "$(uname -r)" = 7.3.0-pjarczak ] || exit 14
[ "$(sha /dev/block/mmcblk0p9)" = 478a9da10024c6ac6426b44ad779b0d6ae66c1355d9b09c2fbdd147ba8140d7f ] || exit 15
[ "$(sha /dev/block/mmcblk0p10)" = f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f ] || exit 16
[ "$(sha /system/vendor/lib/libv4l2_codec2_components.so)" = f735ad9ed95bff53eb0f928476b61b27f9875e23465f3f196bfe5d24877dd7c9 ] || exit 17
[ "$(sha /system/vendor/lib/libril.so)" = 4e6bb8a9465005a01f5bebd92a7e0ad247c0c4b609809c2600547bc97999bcb7 ] || exit 18
[ "$(sha /vendor/bin/hw/android.hardware.power@1.0-service.lt013g)" = cebbb198b87586d65b0bfd2e48a82aee191546a16f64ddf2b4323683e8258a71 ] || exit 19
[ "$(sha /system/bin/screenrecord)" = f9e3669acd0c9d75e4c0f209513c63d8c33867172a995f068a73f00261437634 ] || exit 20
[ "$(settings get system sound_effects_enabled)" = 0 ] || exit 21
[ "$(settings get system lockscreen_sounds_enabled)" = 0 ] || exit 22
[ "$(cat "$m")" = 0 ] || exit 23
[ ! -e "$d" ] || exit 24
[ "$(df -k /data | awk 'NR==2{print $4}')" -ge 110000 ] || exit 25
mkdir -m 700 "$d"
monitor=""
gestures=""
record_pid=""
cleanup(){
 [ -z "$monitor" ] || kill "$monitor" 2>/dev/null || true
 [ -z "$gestures" ] || kill "$gestures" 2>/dev/null || true
 [ -z "$record_pid" ] || kill "$record_pid" 2>/dev/null || true
}
trap cleanup EXIT
printf 'uptime,active,cpu_min_khz,cpu_cur_khz,cpu_max_khz,gpu_min_hz,gpu_cur_hz,gpu_max_hz,dmc_min_hz,c2c_min_hz,right_min_hz,right_cur_hz,mfc_min_hz,cpu_thermal_mC\n' > "$d/clock-temperature.csv"
(
 n=0
 while [ "$n" -lt 78 ]; do
  val="$(cat /sys/class/thermal/thermal_zone1/temp)"
  printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
    "$(cut -d' ' -f1 /proc/uptime)" "$(cat "$m")" \
    "$(cat /sys/devices/system/cpu/cpufreq/policy0/scaling_min_freq)" \
    "$(cat /sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq)" \
    "$(cat /sys/devices/system/cpu/cpufreq/policy0/scaling_max_freq)" \
    "$(cat /sys/class/devfreq/13000000.gpu/min_freq)" \
    "$(cat /sys/class/devfreq/13000000.gpu/cur_freq)" \
    "$(cat /sys/class/devfreq/13000000.gpu/max_freq)" \
    "$(cat /sys/class/devfreq/bus-dmc/min_freq)" \
    "$(cat /sys/class/devfreq/bus-c2c/min_freq)" \
    "$(cat /sys/class/devfreq/bus-rightbus/min_freq)" \
    "$(cat /sys/class/devfreq/bus-rightbus/cur_freq)" \
    "$(cat /sys/class/devfreq/bus-mfc/min_freq)" "$val"
  if [ "$val" -ge 70000 ]; then
   echo G1131_THERMAL_PREEMPT_70C > "$d/thermal-abort.txt"
   [ ! -r "$d/record.pid" ] || kill "$(cat "$d/record.pid")" 2>/dev/null || true
   break
  fi
  n=$((n+1))
  sleep 0.4
 done
) >> "$d/clock-temperature.csv" 2>"$d/monitor-err.txt" &
monitor=$!
am start -a android.settings.SETTINGS > "$d/settings.log" 2>&1
sleep 1
t0="$(cut -d' ' -f1 /proc/uptime)"
/system/bin/screenrecord --size 640x1024 --bit-rate 6000000 --time-limit 30 "$d/real.mp4" > "$d/record-err.txt" 2>&1 &
record_pid=$!
printf '%s\n' "$record_pid" > "$d/record.pid"
(
 sleep 2
 n=0
 while [ "$n" -lt 55 ]; do
  if [ $((n % 2)) -eq 0 ]; then
   input swipe 400 1000 400 250 300 >/dev/null 2>&1 || true
  else
   input swipe 400 250 400 1000 300 >/dev/null 2>&1 || true
  fi
  n=$((n+1))
 done
) &
gestures=$!
set +e
wait "$record_pid"
rc=$?
set -e
t1="$(cut -d' ' -f1 /proc/uptime)"
record_pid=""
kill "$gestures" 2>/dev/null || true
wait "$gestures" 2>/dev/null || true
gestures=""
kill "$monitor" 2>/dev/null || true
wait "$monitor" 2>/dev/null || true
monitor=""
sleep 2
{
 echo "record_rc=$rc"
 echo "start_uptime=$t0"
 echo "end_uptime=$t1"
 echo "mp4_sha256=$(sha "$d/real.mp4")"
 echo "trace_sha256=$(sha "$d/clock-temperature.csv")"
 echo "encoder_end=$(cat "$m")"
 echo "cpu_min_end=$(cat /sys/devices/system/cpu/cpufreq/policy0/scaling_min_freq)"
 echo "gpu_min_end=$(cat /sys/class/devfreq/13000000.gpu/min_freq)"
 echo "dmc_min_end=$(cat /sys/class/devfreq/bus-dmc/min_freq)"
 echo "rightbus_min_end=$(cat /sys/class/devfreq/bus-rightbus/min_freq)"
 echo "thermal_aborted=$(test -f "$d/thermal-abort.txt" && echo 1 || echo 0)"
} > "$d/receipt.txt"
cat "$d/receipt.txt"
[ "$rc" = 0 ] || exit 26
[ ! -e "$d/thermal-abort.txt" ] || exit 27
[ "$(cat "$m")" = 0 ] || exit 28
[ "$(cat /sys/devices/system/cpu/cpufreq/policy0/scaling_min_freq)" = 200000 ] || exit 29
[ "$(cat /sys/class/devfreq/13000000.gpu/min_freq)" = 160000000 ] || exit 30
[ "$(cat /sys/class/devfreq/bus-rightbus/min_freq)" = 100000000 ] || exit 31
echo G1131_FACTORY_PEAK_TRIAL_COMPLETE
