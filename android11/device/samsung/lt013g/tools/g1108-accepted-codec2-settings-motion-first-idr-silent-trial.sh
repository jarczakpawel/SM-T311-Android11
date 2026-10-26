#!/system/bin/sh
# G1108: new-MediaCodec-session first-IDR H264 with Settings screen and scrolling
# READ-ONLY firmware/ROM: uses accepted G1012 and live G1099 screenrecord.
# No audio, reboot, backup, kernel modifications, SIM/PIN/PUK or flash.
set -eu
dir=/data/local/tmp/G1108_FIRST_IDR_PRIVATE
manifest="$dir/G1108_DEVICE_MANIFEST_PRIVATE.tsv"
shasum() { sha256sum "$1" | cut -d ' ' -f 1; }
[ "$(id -u)" = 0 ] || exit 10
[ "$(getprop ro.product.device)" = lt013g ] || exit 11
[ "$(getprop ro.build.version.sdk)" = 30 ] || exit 12
[ "$(getprop sys.boot_completed)" = 1 ] || exit 13
[ "$(getenforce)" = Enforcing ] || exit 14
[ "$(uname -r)" = 7.3.0-pjarczak ] || exit 15
[ "$(shasum /dev/block/mmcblk0p9)" = e42c1b09b4c4d3c91b1a3c5330619d37560287821994d269ba67a970069cd5fd ] || exit 16
[ "$(shasum /dev/block/mmcblk0p10)" = f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f ] || exit 17
[ "$(shasum /system/vendor/lib/libril.so)" = 4e6bb8a9465005a01f5bebd92a7e0ad247c0c4b609809c2600547bc97999bcb7 ] || exit 18
[ "$(shasum /system/vendor/lib/libv4l2_codec2_components.so)" = f735ad9ed95bff53eb0f928476b61b27f9875e23465f3f196bfe5d24877dd7c9 ] || exit 19
[ "$(shasum /system/bin/screenrecord)" = f9e3669acd0c9d75e4c0f209513c63d8c33867172a995f068a73f00261437634 ] || exit 20
[ "$(getprop init.svc.android-hardware-media-c2-v4l2-hal-1-0)" = running ] || exit 21
[ "$(settings get system sound_effects_enabled)" = 0 ] || exit 22
[ "$(settings get system lockscreen_sounds_enabled)" = 0 ] || exit 23
[ "$(cat /sys/module/s5p_mfc/parameters/debug)" = 0 ] || exit 24
[ ! -e "$dir" ] || exit 25
[ "$(df -k /data | awk 'NR==2 {print $4}')" -ge 300000 ] || exit 26
mkdir -m 700 "$dir"
printf 'case\tstatus\tprocess_rc\tbytes\tsha256\n' > "$manifest"
input keyevent KEYCODE_HOME >/dev/null 2>&1 || true
am start -a android.settings.SETTINGS >/dev/null 2>&1 || exit 33
# Restores launcher if the test fails or finishes; no audio controls touched.
gesture_pid=""
cleanup() {
  if [ -n "$gesture_pid" ]; then kill "$gesture_pid" 2>/dev/null || true; fi
  input keyevent KEYCODE_HOME >/dev/null 2>&1 || true
}
trap cleanup EXIT
case_number=0
while [ "$case_number" -lt 8 ]; do
  n="$(printf '%02d' "$case_number")"
  out="$dir/G1108_FIRST_IDR_${n}_PRIVATE.mp4"
  log="$dir/G1108_REC_${n}_PRIVATE.stderr"
  [ ! -e "$out" ] && [ ! -e "$log" ] || exit 27
  set +e
  # Swipe only inside Android Settings. Begin after recorder launch so the
  # first IDR captures the active Settings view before any injected gesture.
  (
    sleep 2
    j=0
    while [ "$j" -lt 8 ]; do
      if [ $((j % 2)) -eq 0 ]; then
        input swipe 400 1010 400 275 360 >/dev/null 2>&1 || true
      else
        input swipe 400 275 400 1010 360 >/dev/null 2>&1 || true
      fi
      j=$((j+1))
    done
  ) >/dev/null 2>&1 &
  gesture_pid=$!
  /system/bin/screenrecord --size 640x1024 --bit-rate 6000000 --time-limit 8 "$out" >"$log" 2>&1
  code=$?
  kill "$gesture_pid" 2>/dev/null || true
  wait "$gesture_pid" 2>/dev/null || true
  gesture_pid=""
  set -e
  [ -f "$out" ] || { echo "G1108_MISSING_CLIP $n"; exit 28; }
  bytes="$(wc -c <"$out")"
  sha="$(shasum "$out")"
  printf '%s\tcompleted\t%s\t%s\t%s\n' "$n" "$code" "$bytes" "$sha" >> "$manifest"
  echo "G1108_RUN_COMPLETE case=$n rc=$code bytes=$bytes sha=$sha"
  [ "$code" -eq 0 ] || { echo "G1108_SCREENRECORD_NONZERO case=$n"; exit 29; }
  case_number=$((case_number+1))
done
[ "$(shasum /system/vendor/lib/libv4l2_codec2_components.so)" = f735ad9ed95bff53eb0f928476b61b27f9875e23465f3f196bfe5d24877dd7c9 ] || exit 30
[ "$(settings get system sound_effects_enabled)" = 0 ] || exit 31
[ "$(settings get system lockscreen_sounds_enabled)" = 0 ] || exit 32
echo G1108_8_SETTINGS_SWIPES_SESSION_COMPLETE_G1012_AND_MUTE_PRESERVED
