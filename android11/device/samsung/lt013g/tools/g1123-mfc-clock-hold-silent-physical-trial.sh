#!/system/bin/sh
# G1123: matched live-H264 physical OPP-neutral CLOCK-GATE A/B.
# Trial BOOT only, original G1012, original RIL, no audio, no backups.
# Source defaults OFF and saves/updates manifest on device.
set -eu
mode="${1:---trial}"
base=/data/local/tmp/G1123_CLOCK_GATE_PRIVATE
self=/data/local/tmp/G1123_CLOCK_GATE_TRIAL.sh
param=/sys/module/s5p_mfc/parameters/v5_clock_hold
svc=android-hardware-media-c2-v4l2-hal-1-0
sha(){ sha256sum "$1" | cut -d ' ' -f 1; }
quiet(){
 [ "$(settings get system sound_effects_enabled)" = 0 ] &&
 [ "$(settings get system lockscreen_sounds_enabled)" = 0 ]
}
cleanup(){
 echo N > "$param" 2>/dev/null || true
 am force-stop com.android.settings >/dev/null 2>&1 || true
 [ -z "${gesture_pid:-}" ] || kill "$gesture_pid" 2>/dev/null || true
}
case "$mode" in
 --watchdog)
   sleep 215
   echo N > "$param" 2>/dev/null || true
   exit 0 ;;
 --trial) ;;
 *) exit 2 ;;
esac
[ "$(id -u)" = 0 ] || exit 10
[ "$(getprop ro.product.device)" = lt013g ] || exit 11
[ "$(getprop ro.build.version.sdk)" = 30 ] || exit 12
[ "$(getprop sys.boot_completed)" = 1 ] || exit 13
[ "$(uname -r)" = 7.3.0-pjarczak ] || exit 14
[ "$(getenforce)" = Enforcing ] || exit 15
[ "$(sha /dev/block/mmcblk0p9)" = 41c54d745b93c2b56b37ba0a19720057ea93ebd85a3f03d29fed3834e7b0b704 ] || exit 16
[ "$(sha /dev/block/mmcblk0p10)" = f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f ] || exit 17
[ "$(sha /system/vendor/lib/libril.so)" = 4e6bb8a9465005a01f5bebd92a7e0ad247c0c4b609809c2600547bc97999bcb7 ] || exit 18
[ "$(sha /system/vendor/lib/libv4l2_codec2_components.so)" = f735ad9ed95bff53eb0f928476b61b27f9875e23465f3f196bfe5d24877dd7c9 ] || exit 19
[ "$(sha /system/bin/screenrecord)" = f9e3669acd0c9d75e4c0f209513c63d8c33867172a995f068a73f00261437634 ] || exit 20
[ "$(getprop init.svc.$svc)" = running ] || exit 21
quiet || exit 22
[ "$(cat /sys/module/s5p_mfc/parameters/debug)" = 0 ] || exit 23
[ -r "$param" ] && [ -w "$param" ] || exit 24
[ "$(cat "$param")" = N ] || exit 25
[ ! -e "$base" ] || exit 26
[ "$(df -k /data | awk 'NR==2 {print $4}')" -ge 170000 ] || exit 27
[ -r "$self" ] || exit 28
mkdir -m 700 "$base"
manifest="$base/G1123_MANIFEST_PRIVATE.tsv"
printf 'index\thold_gate\trecord_rc\tbytes\tsha256\n' > "$manifest"
setsid sh "$self" --watchdog >/dev/null 2>&1 </dev/null &
watchdog_pid=$!
gesture_pid=""
trap 'cleanup; kill "$watchdog_pid" 2>/dev/null || true' EXIT
n=0
while [ "$n" -lt 8 ]; do
 if [ $((n % 2)) -eq 0 ]; then enabled=N; else enabled=Y; fi
 echo "$enabled" > "$param"
 [ "$(cat "$param")" = "$enabled" ] || exit 29
 # Restart only the Codec2 HAL to guarantee MFC device power cycling and
 # relatching gate choice for a NEW encoder session.
 oldpid="$(getprop init.svc_debug_pid.$svc)"
 setprop ctl.restart "$svc"
 t=0
 while [ "$t" -lt 20 ]; do
   sleep 1
   newpid="$(getprop init.svc_debug_pid.$svc)"
   [ -n "$newpid" ] && [ "$newpid" != "$oldpid" ] &&
     [ "$(getprop init.svc.$svc)" = running ] && break
   t=$((t+1))
 done
 [ "$t" -lt 20 ] || exit 30
 am force-stop com.android.settings >/dev/null 2>&1 || true
 am start -a android.settings.SETTINGS > "$base/start-$n.log" 2>&1
 sleep 1
 output="$base/clip-$n-$enabled.mp4"
 /system/bin/screenrecord --size 640x1024 --bit-rate 6000000 --time-limit 12 "$output" > "$base/stderr-$n" 2>&1 &
 recpid=$!
 (
   sleep 2
   i=0
   while [ "$i" -lt 20 ]; do
     if [ $((i % 2)) -eq 0 ]; then
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
 kill "$gesture_pid" 2>/dev/null || true
 wait "$gesture_pid" 2>/dev/null || true
 gesture_pid=""
 [ -s "$output" ] || exit 31
 bytes="$(wc -c < "$output")"
 hash="$(sha "$output")"
 printf '%s\t%s\t%s\t%s\t%s\n' "$n" "$enabled" "$rc" "$bytes" "$hash" >> "$manifest"
 echo "G1123_TRIAL index=$n hold=$enabled rc=$rc bytes=$bytes sha=$hash"
 [ "$rc" -eq 0 ] || exit 32
 n=$((n+1))
done
echo N > "$param"
[ "$(cat "$param")" = N ] || exit 33
dmesg | grep -E 'G1123_MFC_GATE_HELD_ACROSS_FRAMES|s5p-mfc|MFC' | tail -n 200 > "$base/kernel-log-private.txt" || true
grep -c 'G1123_MFC_GATE_HELD_ACROSS_FRAMES' "$base/kernel-log-private.txt" > "$base/hold-count.txt" || true
[ "$(sha /system/vendor/lib/libv4l2_codec2_components.so)" = f735ad9ed95bff53eb0f928476b61b27f9875e23465f3f196bfe5d24877dd7c9 ] || exit 34
quiet || exit 35
echo G1123_CLOCK_GATE_TRIAL_COMPLETE
