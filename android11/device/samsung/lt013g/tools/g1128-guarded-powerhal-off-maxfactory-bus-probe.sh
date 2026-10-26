#!/system/bin/sh
# G1128 fail-closed bounded probe: original G958+G1012, temporarily stop
# old PowerHAL that overwrites devfreq min_freq during UI interaction hints.
# Hold MAX FACTORY OPP: DMC400, LEFTBUS/MFC/RIGHTBUS200, not overclocking.
# Never patch kernel, vendor executable, SELinux or radio; no backup or audio.
set -eu
self=/data/local/tmp/G1128_HAL_OFF_BUS_CAP_TRIAL.sh
dir=/data/local/tmp/G1128_FACTORY_MAX_BUS_PRIVATE
service=vendor.power-hal-1-0
nodes="bus-dmc bus-leftbus bus-mfc bus-rightbus"
sha(){ sha256sum "$1" | cut -d ' ' -f1; }
restore(){
 for node in $nodes; do
  p=/sys/class/devfreq/$node/min_freq
  if [ -r "$dir/$node.original-min" ] && [ -w "$p" ]; then
   value="$(cat "$dir/$node.original-min")"
   echo "$value" > "$p" 2>/dev/null || true
  fi
 done
 setprop ctl.start "$service" || true
 if [ -d "$dir" ]; then
  echo "restored_HAL=$(getprop init.svc.$service)" >> "$dir/restore.txt"
  for node in $nodes; do
   echo "$node min=$(cat /sys/class/devfreq/$node/min_freq 2>/dev/null)" >> "$dir/restore.txt"
  done
 fi
}
if [ "${1:-}" = "--watchdog" ]; then
 sleep 105
 restore
 exit 0
fi
[ "${1:-}" = "--trial" ] || exit 2
[ "$(id -u)" = 0 ] || exit 10
[ "$(getprop ro.product.device)" = lt013g ] || exit 11
[ "$(getprop sys.boot_completed)" = 1 ] || exit 12
[ "$(getenforce)" = Enforcing ] || exit 13
[ "$(sha /dev/block/mmcblk0p9)" = e42c1b09b4c4d3c91b1a3c5330619d37560287821994d269ba67a970069cd5fd ] || exit 14
[ "$(sha /dev/block/mmcblk0p10)" = f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f ] || exit 15
[ "$(sha /system/vendor/lib/libv4l2_codec2_components.so)" = f735ad9ed95bff53eb0f928476b61b27f9875e23465f3f196bfe5d24877dd7c9 ] || exit 16
[ "$(sha /system/vendor/lib/libril.so)" = 4e6bb8a9465005a01f5bebd92a7e0ad247c0c4b609809c2600547bc97999bcb7 ] || exit 17
[ "$(sha /system/bin/screenrecord)" = f9e3669acd0c9d75e4c0f209513c63d8c33867172a995f068a73f00261437634 ] || exit 18
[ "$(sha /vendor/bin/hw/android.hardware.power@1.0-service.lt013g)" = cebbb198b87586d65b0bfd2e48a82aee191546a16f64ddf2b4323683e8258a71 ] || exit 19
[ "$(getprop init.svc.$service)" = running ] || exit 20
[ "$(settings get system sound_effects_enabled)" = 0 ] || exit 21
[ "$(settings get system lockscreen_sounds_enabled)" = 0 ] || exit 22
[ "$(df -k /data | awk 'NR==2 {print $4}')" -ge 120000 ] || exit 23
[ ! -e "$dir" ] || exit 24
[ "$(getprop debug.t311.mfc_rightbus_hold)" != 1 ] || exit 25
for node in $nodes; do
 [ -w "/sys/class/devfreq/$node/min_freq" ] || exit 26
done
[ "$(cat /sys/class/devfreq/bus-dmc/max_freq)" = 400000000 ] || exit 27
for node in bus-leftbus bus-mfc bus-rightbus; do
 [ "$(cat /sys/class/devfreq/$node/max_freq)" = 200000000 ] || exit 28
done
mkdir -m 700 "$dir"
for node in $nodes; do
 cat "/sys/class/devfreq/$node/min_freq" > "$dir/$node.original-min"
 cat "/sys/class/devfreq/$node/trans_stat" > "$dir/$node-before.txt"
done
gesture_pid=""
watchdog_pid=""
monitor_pid=""
cleanup(){
 [ -z "$gesture_pid" ] || kill "$gesture_pid" 2>/dev/null || true
 [ -z "$monitor_pid" ] || kill "$monitor_pid" 2>/dev/null || true
 restore
 [ -z "$watchdog_pid" ] || kill "$watchdog_pid" 2>/dev/null || true
}
trap cleanup EXIT
setsid sh "$self" --watchdog >/dev/null 2>&1 </dev/null &
watchdog_pid=$!
setprop ctl.stop "$service"
n=0
while [ "$n" -lt 12 ] && [ "$(getprop init.svc.$service)" != stopped ]; do sleep 1; n=$((n+1)); done
[ "$(getprop init.svc.$service)" = stopped ] || exit 29
echo 400000000 > /sys/class/devfreq/bus-dmc/min_freq
for node in bus-leftbus bus-mfc bus-rightbus; do echo 200000000 > /sys/class/devfreq/$node/min_freq; done
sleep 2
[ "$(cat /sys/class/devfreq/bus-dmc/cur_freq)" = 400000000 ] || exit 30
for node in bus-leftbus bus-mfc bus-rightbus; do
 [ "$(cat /sys/class/devfreq/$node/min_freq)" = 200000000 ] || exit 31
 [ "$(cat /sys/class/devfreq/$node/cur_freq)" = 200000000 ] || exit 32
done
echo G1128_ALL_FACTORY_OPP_MAX_CONFIRMED
am start -a android.settings.SETTINGS > "$dir/settings.log" 2>&1
sleep 2
[ "$(getprop init.svc.$service)" = stopped ] || exit 33
for node in bus-leftbus bus-mfc bus-rightbus; do [ "$(cat /sys/class/devfreq/$node/min_freq)" = 200000000 ] || exit 34; done
printf 'uptime,rightbus,mfc,leftbus,dmc,rightbus_min,power_hal_service\n' > "$dir/freq-trace.csv"
(
 n=0
 while [ "$n" -lt 82 ]; do
  printf '%s,%s,%s,%s,%s,%s,%s\n' "$(cut -d ' ' -f1 /proc/uptime)" \
    "$(cat /sys/class/devfreq/bus-rightbus/cur_freq)" \
    "$(cat /sys/class/devfreq/bus-mfc/cur_freq)" \
    "$(cat /sys/class/devfreq/bus-leftbus/cur_freq)" \
    "$(cat /sys/class/devfreq/bus-dmc/cur_freq)" \
    "$(cat /sys/class/devfreq/bus-rightbus/min_freq)" \
    "$(getprop init.svc.$service)"
  n=$((n+1)); sleep 0.4
 done
) >> "$dir/freq-trace.csv" 2>"$dir/freq-stderr.txt" &
monitor_pid=$!
t0="$(cut -d ' ' -f1 /proc/uptime)"
/system/bin/screenrecord --size 640x1024 --bit-rate 6000000 --time-limit 52 "$dir/real.mp4" > "$dir/record.stderr" 2>&1 &
recpid=$!
(
 sleep 2
 n=0
 while [ "$n" -lt 92 ]; do
  if [ $((n%2)) -eq 0 ]; then input swipe 400 1000 400 250 350 >/dev/null 2>&1 || true
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
for node in $nodes; do
 cat "/sys/class/devfreq/$node/trans_stat" > "$dir/$node-after.txt"
done
{
 echo "record_rc=$rc"
 echo "start_uptime=$t0"
 echo "end_uptime=$t1"
 echo "video_sha=$(sha "$dir/real.mp4")"
 echo "trace_sha=$(sha "$dir/freq-trace.csv")"
 echo "power_hal_final=$(getprop init.svc.$service)"
 echo "rightbus_min_final=$(cat /sys/class/devfreq/bus-rightbus/min_freq)"
 echo "rightbus_cur_final=$(cat /sys/class/devfreq/bus-rightbus/cur_freq)"
 echo "dmc_cur_final=$(cat /sys/class/devfreq/bus-dmc/cur_freq)"
} > "$dir/receipt.txt"
cat "$dir/receipt.txt"
[ "$rc" = 0 ] || exit 35
[ "$(getprop init.svc.$service)" = stopped ] || exit 36
[ "$(cat /sys/class/devfreq/bus-rightbus/cur_freq)" = 200000000 ] || exit 37
echo G1128_FACTORY_MAX_BUS_PROBE_COMPLETE
