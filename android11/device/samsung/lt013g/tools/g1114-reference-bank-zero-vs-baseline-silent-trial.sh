#!/system/bin/sh
set -eu
sha() { sha256sum "$1" | cut -d ' ' -f1; }
[ "$(id -u)" = 0 ] || exit 10
[ "$(getprop ro.product.device)" = lt013g ] || exit 11
[ "$(getprop ro.build.version.sdk)" = 30 ] || exit 12
[ "$(getprop sys.boot_completed)" = 1 ] || exit 13
[ "$(uname -r)" = 7.3.0-pjarczak ] || exit 14
[ "$(getenforce)" = Enforcing ] || exit 15
[ "$(sha /dev/block/mmcblk0p9)" = d961caeedcbc1d6480254d6a3946effabff8029c7f4f2c608d0e59024c02a09a ] || exit 16
[ "$(sha /dev/block/mmcblk0p10)" = f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f ] || exit 17
[ "$(sha /system/vendor/lib/libril.so)" = 4e6bb8a9465005a01f5bebd92a7e0ad247c0c4b609809c2600547bc97999bcb7 ] || exit 18
[ "$(sha /system/vendor/lib/libv4l2_codec2_components.so)" = f735ad9ed95bff53eb0f928476b61b27f9875e23465f3f196bfe5d24877dd7c9 ] || exit 19
[ "$(sha /system/bin/screenrecord)" = f9e3669acd0c9d75e4c0f209513c63d8c33867172a995f068a73f00261437634 ] || exit 20
[ "$(getprop init.svc.android-hardware-media-c2-v4l2-hal-1-0)" = running ] || exit 21
[ "$(settings get system sound_effects_enabled)" = 0 ] || exit 22
[ "$(settings get system lockscreen_sounds_enabled)" = 0 ] || exit 23
[ "$(cat /sys/module/s5p_mfc/parameters/debug)" = 0 ] || exit 24
parameter=/sys/module/s5p_mfc/parameters/g1114_zero_h264_refs
[ -f "$parameter" ] || exit 25
[ "$(cat "$parameter")" = N ] || exit 26
pm path org.smt311.redraw | grep -q '^package:' || exit 27
dir=/data/local/tmp/G1114_REFERENCE_ZERO_PRIVATE
[ ! -e "$dir" ] || exit 28
[ "$(df -k /data | awk 'NR==2 {print $4}')" -gt 200000 ] || exit 29
mkdir -m 700 "$dir"
manifest="$dir/G1114_DEVICE_MANIFEST_PRIVATE.tsv"
printf 'index\tzero_refs\tinterval_ms\tstatus\tbytes\tsha256\n' > "$manifest"
cleanup() {
  echo N > "$parameter" 2>/dev/null || true
  am force-stop org.smt311.redraw >/dev/null 2>&1 || true
  input keyevent KEYCODE_HOME >/dev/null 2>&1 || true
}
trap cleanup EXIT
i=0
while [ "$i" -lt 8 ]; do
  case "$i" in
    0|2|4|6) enabled=N ;;
    *) enabled=Y ;;
  esac
  case "$i" in
    0|1|4|5) interval=66 ;;
    *) interval=133 ;;
  esac
  echo "$enabled" > "$parameter"
  [ "$(cat "$parameter")" = "$enabled" ] || exit 30
  n="$(printf '%02d' "$i")"
  am force-stop org.smt311.redraw >/dev/null 2>&1 || true
  am start -W -n org.smt311.redraw/.MainActivity --es mode static --ei interval_ms "$interval" > "$dir/G1114_START_${n}.log" 2>&1 || exit 31
  sleep 1
  pidof org.smt311.redraw >/dev/null || exit 32
  output="$dir/G1114_${n}_${enabled}_${interval}ms_PRIVATE.mp4"
  set +e
  /system/bin/screenrecord --size 640x1024 --bit-rate 6000000 --time-limit 6 "$output" > "$dir/G1114_REC_${n}.stderr" 2>&1
  rc=$?
  set -e
  [ -s "$output" ] || exit 33
  bytes="$(wc -c < "$output")"
  hash="$(sha "$output")"
  printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$n" "$enabled" "$interval" "$rc" "$bytes" "$hash" >> "$manifest"
  echo "G1114_RECORD index=$n zero=$enabled interval=$interval rc=$rc bytes=$bytes sha=$hash"
  [ "$rc" -eq 0 ] || exit 34
  am force-stop org.smt311.redraw >/dev/null 2>&1 || true
  i=$((i+1))
done
echo N > "$parameter"
[ "$(cat "$parameter")" = N ] || exit 35
dmesg > "$dir/G1114_KERNEL_LOG_PRIVATE.txt"
logcat -b main -d -v brief -s G1112:I > "$dir/G1114_APP_LOG_PRIVATE.txt"
[ "$(sha /system/vendor/lib/libv4l2_codec2_components.so)" = f735ad9ed95bff53eb0f928476b61b27f9875e23465f3f196bfe5d24877dd7c9 ] || exit 36
[ "$(settings get system sound_effects_enabled)" = 0 ] || exit 37
[ "$(settings get system lockscreen_sounds_enabled)" = 0 ] || exit 38
echo G1114_REFERENCE_ZERO_TRIAL_COMPLETE
