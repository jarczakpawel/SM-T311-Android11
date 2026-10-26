#!/system/bin/sh
set -eu
mode="${1:---trial}"
[ "$mode" = --trial ] || exit 2
suffix=RATE
total=3
dir="/data/local/tmp/G1112_${suffix}_PRIVATE"
sha() { sha256sum "$1" | cut -d ' ' -f1; }
[ "$(id -u)" = 0 ] || exit 10
[ "$(getprop ro.product.device)" = lt013g ] || exit 11
[ "$(getprop ro.build.version.sdk)" = 30 ] || exit 12
[ "$(getprop sys.boot_completed)" = 1 ] || exit 13
[ "$(getenforce)" = Enforcing ] || exit 14
[ "$(uname -r)" = 7.3.0-pjarczak ] || exit 15
[ "$(sha /dev/block/mmcblk0p9)" = e42c1b09b4c4d3c91b1a3c5330619d37560287821994d269ba67a970069cd5fd ] || exit 16
[ "$(sha /dev/block/mmcblk0p10)" = f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f ] || exit 17
[ "$(sha /system/vendor/lib/libril.so)" = 4e6bb8a9465005a01f5bebd92a7e0ad247c0c4b609809c2600547bc97999bcb7 ] || exit 18
[ "$(sha /system/vendor/lib/libv4l2_codec2_components.so)" = f735ad9ed95bff53eb0f928476b61b27f9875e23465f3f196bfe5d24877dd7c9 ] || exit 19
[ "$(sha /system/bin/screenrecord)" = f9e3669acd0c9d75e4c0f209513c63d8c33867172a995f068a73f00261437634 ] || exit 20
[ "$(getprop init.svc.android-hardware-media-c2-v4l2-hal-1-0)" = running ] || exit 21
[ "$(settings get system sound_effects_enabled)" = 0 ] || exit 22
[ "$(settings get system lockscreen_sounds_enabled)" = 0 ] || exit 23
[ "$(cat /sys/module/s5p_mfc/parameters/debug)" = 0 ] || exit 24
pm path org.smt311.redraw | grep -q '^package:' || exit 25
[ ! -e "$dir" ] || exit 26
[ "$(df -k /data | awk 'NR==2 {print $4}')" -gt 200000 ] || exit 27
mkdir -m 700 "$dir"
manifest="$dir/G1112_${suffix}_DEVICE_MANIFEST_PRIVATE.tsv"
printf 'index\tmode\tinterval_ms\trc\tbytes\tsha256\n' > "$manifest"
cleanup() {
  am force-stop org.smt311.redraw >/dev/null 2>&1 || true
  input keyevent KEYCODE_HOME >/dev/null 2>&1 || true
}
trap cleanup EXIT
i=0
while [ "$i" -lt "$total" ]; do
  picture=static
  case "$i" in 0) interval=133 ;; 1) interval=66 ;; 2) interval=33 ;; esac
  n="$(printf '%02d' "$i")"
  am force-stop org.smt311.redraw >/dev/null 2>&1 || true
  am start -W -n org.smt311.redraw/.MainActivity --es mode "$picture" --ei interval_ms "$interval" > "$dir/G1112_START_${n}.log" 2>&1 || exit 31
  sleep 1
  dumpsys activity activities | grep -q 'org.smt311.redraw/.MainActivity' || exit 32
  output="$dir/G1112_${suffix}_${n}_${picture}_${interval}ms_PRIVATE.mp4"
  stderr="$dir/G1112_REC_${n}.stderr"
  set +e
  /system/bin/screenrecord --size 640x1024 --bit-rate 6000000 --time-limit 6 "$output" > "$stderr" 2>&1
  rc=$?
  set -e
  [ -f "$output" ] || exit 33
  bytes="$(wc -c < "$output")"
  hash="$(sha "$output")"
  printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$n" "$picture" "$interval" "$rc" "$bytes" "$hash" >> "$manifest"
  echo "G1112_RECORD index=$n mode=$picture interval=$interval exit=$rc bytes=$bytes sha=$hash"
  [ "$rc" -eq 0 ] || exit 34
  am force-stop org.smt311.redraw >/dev/null 2>&1 || true
  i=$((i+1))
done
logcat -d -v brief -s G1112:I G1112:E > "$dir/G1112_ACTIVITY_PRIVATE.log"
[ "$(sha /system/vendor/lib/libv4l2_codec2_components.so)" = f735ad9ed95bff53eb0f928476b61b27f9875e23465f3f196bfe5d24877dd7c9 ] || exit 35
[ "$(settings get system sound_effects_enabled)" = 0 ] || exit 36
[ "$(settings get system lockscreen_sounds_enabled)" = 0 ] || exit 37
[ "$(cat /sys/module/s5p_mfc/parameters/debug)" = 0 ] || exit 38
echo "G1112_${suffix}_COMPLETE"
