#!/system/bin/sh
# G1125 silent read-only Exynos4 bus/DVFS telemetry during one real MFCv5 recording.
# Zero sysfs frequency/voltage writes, flash, backup, audio, modem calls.
set -eu
sha(){ sha256sum "$1" | cut -d ' ' -f1; }
[ "$(id -u)" = 0 ] || exit 10
[ "$(getprop ro.product.device)" = lt013g ] || exit 11
[ "$(getprop sys.boot_completed)" = 1 ] || exit 12
[ "$(uname -r)" = 7.3.0-pjarczak ] || exit 13
[ "$(getenforce)" = Enforcing ] || exit 14
[ "$(sha /dev/block/mmcblk0p9)" = e42c1b09b4c4d3c91b1a3c5330619d37560287821994d269ba67a970069cd5fd ] || exit 15
[ "$(sha /dev/block/mmcblk0p10)" = f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f ] || exit 16
[ "$(sha /system/vendor/lib/libv4l2_codec2_components.so)" = f735ad9ed95bff53eb0f928476b61b27f9875e23465f3f196bfe5d24877dd7c9 ] || exit 17
[ "$(sha /system/vendor/lib/libril.so)" = 4e6bb8a9465005a01f5bebd92a7e0ad247c0c4b609809c2600547bc97999bcb7 ] || exit 18
[ "$(sha /system/bin/screenrecord)" = f9e3669acd0c9d75e4c0f209513c63d8c33867172a995f068a73f00261437634 ] || exit 19
[ "$(settings get system sound_effects_enabled)" = 0 ] || exit 20
[ "$(settings get system lockscreen_sounds_enabled)" = 0 ] || exit 21
[ "$(getprop init.svc.android-hardware-media-c2-v4l2-hal-1-0)" = running ] || exit 22
[ "$(cat /sys/module/s5p_mfc/parameters/debug)" = 0 ] || exit 23
dir=/data/local/tmp/G1125_BUS_TELEMETRY_PRIVATE
[ ! -e "$dir" ] || exit 24
[ "$(df -k /data | awk 'NR==2 {print $4}')" -ge 100000 ] || exit 25
for c in mfc dmc leftbus rightbus; do
 [ -r "/sys/class/devfreq/bus-$c/cur_freq" ] || exit 26
 [ -r "/sys/class/devfreq/bus-$c/trans_stat" ] || exit 27
done
mkdir -m 700 "$dir"
for c in mfc dmc leftbus rightbus; do cat "/sys/class/devfreq/bus-$c/trans_stat" > "$dir/$c-before.txt"; done
monitor_pid=""
gesture_pid=""
cleanup(){
 [ -z "$monitor_pid" ] || kill "$monitor_pid" 2>/dev/null || true
 [ -z "$gesture_pid" ] || kill "$gesture_pid" 2>/dev/null || true
 am force-stop com.android.settings >/dev/null 2>&1 || true
}
trap cleanup EXIT
printf 'uptime_s,mfc_hz,dmc_hz,leftbus_hz,rightbus_hz\n' > "$dir/bus-trace-private.csv"
(
 k=0
 while [ "$k" -lt 150 ]; do
  now="$(cut -d ' ' -f1 /proc/uptime)"
  a="$(cat /sys/class/devfreq/bus-mfc/cur_freq)"
  b="$(cat /sys/class/devfreq/bus-dmc/cur_freq)"
  c="$(cat /sys/class/devfreq/bus-leftbus/cur_freq)"
  d="$(cat /sys/class/devfreq/bus-rightbus/cur_freq)"
  printf '%s,%s,%s,%s,%s\n' "$now" "$a" "$b" "$c" "$d"
  k=$((k+1))
  sleep 0.2
 done
) >> "$dir/bus-trace-private.csv" 2> "$dir/bus-trace.stderr" &
monitor_pid=$!
am start -a android.settings.SETTINGS > "$dir/settings-start.log" 2>&1 || exit 28
sleep 1
t0="$(cut -d ' ' -f1 /proc/uptime)"
/system/bin/screenrecord --size 640x1024 --bit-rate 6000000 --time-limit 22 "$dir/real.mp4" > "$dir/record.stderr" 2>&1 &
recpid=$!
(
 sleep 2
 i=0
 while [ "$i" -lt 34 ]; do
  if [ $((i%2)) -eq 0 ]; then
   input swipe 400 1000 400 250 350 >/dev/null 2>&1 || true
  else
   input swipe 400 250 400 1000 350 >/dev/null 2>&1 || true
  fi
  i=$((i+1))
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
for c in mfc dmc leftbus rightbus; do cat "/sys/class/devfreq/bus-$c/trans_stat" > "$dir/$c-after.txt"; done
dmesg | grep -E 'SMT311_G582_OPP_REJECT|exynos-bus.*(failed|error)|regulator.*failed|s5p-mfc.*(failed|error)' | tail -n 30 > "$dir/opp-errors-private.txt" || true
[ -s "$dir/real.mp4" ] || exit 29
{
 echo "G1125_record_rc=$rc"
 echo "G1125_screenrecord_start_uptime_s=$t0"
 echo "G1125_screenrecord_end_uptime_s=$t1"
 echo "G1125_real_video_sha256=$(sha "$dir/real.mp4")"
 echo "G1125_telemetry_sha256=$(sha "$dir/bus-trace-private.csv")"
 echo "G1125_kernel_boot_sha256=$(sha /dev/block/mmcblk0p9)"
} > "$dir/receipt-private.txt"
cat "$dir/receipt-private.txt"
[ "$rc" -eq 0 ] || exit 30
[ "$(settings get system sound_effects_enabled)" = 0 ] || exit 31
[ "$(settings get system lockscreen_sounds_enabled)" = 0 ] || exit 32
echo G1125_READONLY_BUS_H264_TELEMETRY_COMPLETE
