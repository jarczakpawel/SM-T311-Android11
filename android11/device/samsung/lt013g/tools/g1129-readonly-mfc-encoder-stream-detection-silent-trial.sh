#!/system/bin/sh
# G1129: SOURCE-PINNED read-only kernel MFC encoder stream count.
# This is a physical test of detection, NOT the new PowerHAL release.
# No audio, no backup, no modem; BOOT always restored by guarded TWRP ZIP.
set -eu
dir=/data/local/tmp/G1129_MFC_ENCODER_COUNT_PRIVATE
sys=/sys/devices/platform/soc/13400000.codec/active_encoders
sha(){ sha256sum "$1" | cut -d ' ' -f1; }
[ "$(id -u)" = 0 ] || exit 10
[ "$(getprop ro.product.device)" = lt013g ] || exit 11
[ "$(getprop sys.boot_completed)" = 1 ] || exit 12
[ "$(getenforce)" = Enforcing ] || exit 13
[ "$(sha /dev/block/mmcblk0p9)" = a29a4d874d1019f0f6206e2a29c3436d48598c777e1d8ecc9329501f3d917623 ] || exit 14
[ "$(sha /dev/block/mmcblk0p10)" = f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f ] || exit 15
[ "$(sha /system/vendor/lib/libv4l2_codec2_components.so)" = f735ad9ed95bff53eb0f928476b61b27f9875e23465f3f196bfe5d24877dd7c9 ] || exit 16
[ "$(sha /system/vendor/lib/libril.so)" = 4e6bb8a9465005a01f5bebd92a7e0ad247c0c4b609809c2600547bc97999bcb7 ] || exit 17
[ "$(sha /system/bin/screenrecord)" = f9e3669acd0c9d75e4c0f209513c63d8c33867172a995f068a73f00261437634 ] || exit 18
[ "$(settings get system sound_effects_enabled)" = 0 ] || exit 19
[ "$(settings get system lockscreen_sounds_enabled)" = 0 ] || exit 20
[ "$(getprop init.svc.android-hardware-media-c2-v4l2-hal-1-0)" = running ] || exit 21
[ "$(sha /vendor/bin/hw/android.hardware.power@1.0-service.lt013g)" = cebbb198b87586d65b0bfd2e48a82aee191546a16f64ddf2b4323683e8258a71 ] || exit 22
[ -r "$sys" ] || exit 23
[ "$(cat "$sys")" = 0 ] || exit 24
[ ! -e "$dir" ] || exit 25
mkdir -m 700 "$dir"
monitor_pid=""
cleanup(){
 [ -z "$monitor_pid" ] || kill "$monitor_pid" 2>/dev/null || true
 am force-stop com.android.settings >/dev/null 2>&1 || true
}
trap cleanup EXIT
printf 'uptime,active_encoders\n' > "$dir/mfc-active.csv"
(
 i=0
 while [ "$i" -lt 52 ]; do
  printf '%s,%s\n' "$(cut -d ' ' -f1 /proc/uptime)" "$(cat "$sys")"
  i=$((i+1)); sleep 0.2
 done
) >> "$dir/mfc-active.csv" 2> "$dir/monitor.stderr" &
monitor_pid=$!
am start -a android.settings.SETTINGS > "$dir/settings.log" 2>&1
sleep 1
t0="$(cut -d ' ' -f1 /proc/uptime)"
/system/bin/screenrecord --size 640x1024 --bit-rate 6000000 --time-limit 11 "$dir/real.mp4" > "$dir/screenrecord.stderr" 2>&1 &
recpid=$!
set +e
wait "$recpid"
rc=$?
set -e
t1="$(cut -d ' ' -f1 /proc/uptime)"
kill "$monitor_pid" 2>/dev/null || true
wait "$monitor_pid" 2>/dev/null || true
monitor_pid=""
sleep 2
count_end="$(cat "$sys")"
[ -s "$dir/real.mp4" ] || exit 26
{
 echo "G1129_screenrecord_exit=$rc"
 echo "G1129_video_start_uptime=$t0"
 echo "G1129_video_stop_uptime=$t1"
 echo "G1129_original_video_sha256=$(sha "$dir/real.mp4")"
 echo "G1129_counter_trace_sha256=$(sha "$dir/mfc-active.csv")"
 echo "G1129_counter_after_stop=$count_end"
} > "$dir/receipt.txt"
cat "$dir/receipt.txt"
[ "$rc" = 0 ] || exit 27
[ "$count_end" = 0 ] || exit 28
grep -q ',[1-9][0-9]*$' "$dir/mfc-active.csv" || exit 29
echo G1129_KERNEL_MFC_ACTUAL_ENCODER_STREAM_DETECT_PASS
