#!/system/bin/sh
# G1095 read-only MFC IRQ reason experiment on accepted G958, NO FLASH/backup/audio.
# Temporarily sets *existing* kernel s5p_mfc debug module parameter 0->1;
# detached watchdog and EXIT both restore 0. Writes no persistent system files.
set -eu
mode="${1:---trial}"
knob=/sys/module/s5p_mfc/parameters/debug
flag=/data/local/tmp/G1095_MFC_DEBUG_ACTIVE
log=/data/local/tmp/G1095_IRQ_KMSG_PRIVATE.txt
video=/data/local/tmp/G1095_IRQ_VIDEO_PRIVATE.mp4
stderr=/data/local/tmp/G1095_IRQ_RECORDER_STDERR_PRIVATE.txt
result=/data/local/tmp/G1095_IRQ_TRIAL_PRIVATE.txt
service=android-hardware-media-c2-v4l2-hal-1-0
oldc2=f735ad9ed95bff53eb0f928476b61b27f9875e23465f3f196bfe5d24877dd7c9
screenrec=4bdbac4413ce8a2a816139e09c1546750793eac94299a71d51861a551891f3d6
file_sha() { sha256sum "$1" | cut -d ' ' -f 1; }
restore() {
  if [ -e "$flag" ] || [ "$(cat "$knob")" = 1 ]; then
    echo 0 > "$knob"
  fi
  [ "$(cat "$knob")" = 0 ] || { echo G1095_DEBUG_NOT_RESTORED; return 31; }
  rm -f "$flag"
  echo G1095_MFC_DEBUG_RESTORED_0
}
case "$mode" in
  --restore) restore; exit $? ;;
  --watchdog)
    sleep 135
    if [ -e "$flag" ]; then
      restore >>"$result" 2>&1
    else
      echo G1095_WATCHDOG_INERT >>"$result"
    fi
    exit 0;;
  --trial) ;;
  *) exit 2;;
esac
[ "$(id -u)" = 0 ] || exit 10
[ "$(getprop ro.product.device)" = lt013g ] || exit 11
[ "$(getprop ro.build.version.sdk)" = 30 ] || exit 12
[ "$(getprop sys.boot_completed)" = 1 ] || exit 13
[ "$(getenforce)" = Enforcing ] || exit 14
[ "$(getprop init.svc.$service)" = running ] || exit 15
[ "$(uname -r)" = 7.3.0-pjarczak ] || exit 16
[ "$(cat "$knob")" = 0 ] || exit 17
[ "$(file_sha /dev/block/mmcblk0p9)" = e42c1b09b4c4d3c91b1a3c5330619d37560287821994d269ba67a970069cd5fd ] || exit 18
[ "$(file_sha /dev/block/mmcblk0p10)" = f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f ] || exit 19
[ "$(file_sha /system/vendor/lib/libril.so)" = 4e6bb8a9465005a01f5bebd92a7e0ad247c0c4b609809c2600547bc97999bcb7 ] || exit 20
[ "$(file_sha /system/vendor/lib/libv4l2_codec2_components.so)" = "$oldc2" ] || exit 21
[ "$(file_sha /system/bin/screenrecord)" = "$screenrec" ] || exit 22
[ "$(settings get system sound_effects_enabled)" = 0 ] || exit 23
[ "$(settings get system lockscreen_sounds_enabled)" = 0 ] || exit 24
[ ! -e "$flag" ] || exit 25
[ ! -e "$video" ] && [ ! -e "$log" ] || exit 26
command -v dmesg >/dev/null || exit 27
# Dismiss lockscreen only, no audio. Do not change any build, HAL or bank.
wm dismiss-keyguard
input keyevent KEYCODE_HOME >/dev/null 2>&1
dumpsys window | grep -F 'isKeyguardShowing=false' >/dev/null || exit 28
setsid sh "$0" --watchdog >/data/local/tmp/G1095_WATCHDOG_PRIVATE.txt 2>&1 </dev/null &
touch "$flag"
log_pid=""
gesture_pid=""
cleanup() {
  if [ -n "$gesture_pid" ]; then
    kill "$gesture_pid" 2>/dev/null || true
    wait "$gesture_pid" 2>/dev/null || true
  fi
  restore
  if [ -n "$log_pid" ]; then
    kill "$log_pid" 2>/dev/null || true
    wait "$log_pid" 2>/dev/null || true
  fi
}
trap 'cleanup' EXIT
# dmesg -w logs current kernel ring and FOLLOWS; never clears messages.
dmesg -w -s 1048576 >"$log" 2>&1 </dev/null &
log_pid="$!"
sleep 1
printf '<6>G1095_DIAG_BEGIN\n' >/dev/kmsg
echo 1 > "$knob"
[ "$(cat "$knob")" = 1 ] || { echo G1095_DEBUG_ENABLE_FAILED; exit 29; }
echo G1095_LIVE_IRQ_DEBUG1_ACTIVE
am start -a android.settings.SETTINGS >/dev/null 2>&1
screenrecord --size 640x1024 --bit-rate 6000000 --time-limit 70 "$video" >"$stderr" 2>&1 &
rec_pid="$!"
(
 sleep 2
 i=0
 while [ "$i" -lt 176 ]; do
   if [ $((i % 2)) -eq 0 ]; then
     input swipe 400 1010 400 275 390 >/dev/null 2>&1 || true
   else
     input swipe 400 275 400 1010 390 >/dev/null 2>&1 || true
   fi
   i=$((i+1))
 done
) &
gesture_pid="$!"
wait "$rec_pid"
rc="$?"
kill "$gesture_pid" 2>/dev/null || true
wait "$gesture_pid" 2>/dev/null || true
gesture_pid=""
printf '<6>G1095_DIAG_END\n' >/dev/kmsg
echo "G1095_SCREENRECORD_EXIT=$rc"
ls -l "$video"
[ -s "$video" ] || exit 32
input keyevent KEYCODE_HOME >/dev/null 2>&1 || true
exit "$rc"
