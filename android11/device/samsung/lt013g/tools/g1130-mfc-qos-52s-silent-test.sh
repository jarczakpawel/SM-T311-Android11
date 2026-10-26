#!/system/bin/sh
# G1130 read-only measurements while real HW MFC screenrecord runs.
set -eu
d=/data/local/tmp/G1130_MFC_QOS_PRIVATE
m=/sys/devices/platform/soc/13400000.codec/active_encoders
[ "$(id -u)" = 0 ] || exit 10
[ "$(getprop ro.product.device)" = lt013g ] || exit 11
[ "$(getprop sys.boot_completed)" = 1 ] || exit 12
[ "$(getenforce)" = Enforcing ] || exit 13
[ "$(sha256sum /dev/block/mmcblk0p9 | cut -d' ' -f1)" = acc2a0579e0f4975da3b569941534f5b572ad9a7ae2bf4409fd88b5f633dd565 ] || exit 14
[ "$(sha256sum /system/vendor/lib/libv4l2_codec2_components.so | cut -d' ' -f1)" = f735ad9ed95bff53eb0f928476b61b27f9875e23465f3f196bfe5d24877dd7c9 ] || exit 15
[ "$(sha256sum /system/bin/screenrecord | cut -d' ' -f1)" = f9e3669acd0c9d75e4c0f209513c63d8c33867172a995f068a73f00261437634 ] || exit 16
[ "$(settings get system sound_effects_enabled)" = 0 ] || exit 17
[ "$(settings get system lockscreen_sounds_enabled)" = 0 ] || exit 18
[ -r "$m" ] && [ "$(cat "$m")" = 0 ] || exit 19
[ ! -e "$d" ] || exit 20
mkdir -m 700 "$d"
printf 'uptime,encoder,dmc_min,right_min,left_min,mfc_min,gpu_min,right_cur\n' > "$d/qos.csv"
(
 i=0
 while [ "$i" -lt 100 ]; do
  printf '%s,%s,%s,%s,%s,%s,%s,%s\n' "$(cut -d' ' -f1 /proc/uptime)" "$(cat "$m")" "$(cat /sys/class/devfreq/bus-dmc/min_freq)" "$(cat /sys/class/devfreq/bus-rightbus/min_freq)" "$(cat /sys/class/devfreq/bus-leftbus/min_freq)" "$(cat /sys/class/devfreq/bus-mfc/min_freq)" "$(cat /sys/class/devfreq/13000000.gpu/min_freq)" "$(cat /sys/class/devfreq/bus-rightbus/cur_freq)"
  i=$((i+1))
  sleep 0.3
 done
) >> "$d/qos.csv" 2> "$d/monitor-err.txt" &
mon=$!
am start -a android.settings.SETTINGS >/dev/null 2>&1
sleep 1
t0="$(cut -d' ' -f1 /proc/uptime)"
/system/bin/screenrecord --size 640x1024 --bit-rate 6000000 --time-limit 52 "$d/real.mp4" > "$d/rec-err.txt" 2>&1 &
rec=$!
(
 sleep 2
 i=0
 while [ "$i" -lt 86 ]; do
  input swipe 400 1000 400 250 350 >/dev/null 2>&1 || true
  i=$((i+1))
 done
) &
swipes=$!
set +e
wait "$rec"
rc=$?
set -e
kill "$swipes" 2>/dev/null || true
kill "$mon" 2>/dev/null || true
wait "$mon" 2>/dev/null || true
sleep 3
{
 echo record_rc=$rc
 echo start_uptime=$t0
 echo end_uptime=$(cut -d' ' -f1 /proc/uptime)
 sha256sum "$d/real.mp4" "$d/qos.csv"
 echo encoder_after=$(cat "$m")
 echo dmc_min_after=$(cat /sys/class/devfreq/bus-dmc/min_freq)
 echo right_min_after=$(cat /sys/class/devfreq/bus-rightbus/min_freq)
} > "$d/receipt.txt"
cat "$d/receipt.txt"
[ "$rc" -eq 0 ] && [ "$(cat "$m")" = 0 ]
