#!/system/bin/sh
# G998: temporary, SHA-guarded, reversible HW decoder CMA-pressure trial ONLY.
# Trial does not flash BOOT/RECOVERY/SYSTEM, touch SIM, enter PIN, or make backups.
# All physical test media were previously validated with strict ffmpeg.
set -eu
mode="${1:---trial}"
target=/system/vendor/lib/libv4l2_codec2_components.so
stage=/data/local/tmp/G998_decoder_trial.so
flag=/data/local/tmp/G998_DECODER_TRIAL_ACTIVE
result=/data/local/tmp/G998_DECODER_PRIVATE.log
telemetry=/data/local/tmp/G998_CMA_PRIVATE.txt
logs=/data/local/tmp/G998_DECODER_LOGCAT_PRIVATE.txt
old=6137f22dad0e0d7670020d50fb86266c7755b24dd546cb7ce3830a2bb89f75f6
new=5fd1d1735e44ef89c0c3ea7139213f5495b2218480bfbf94867b48285b112837
radio=4e6bb8a9465005a01f5bebd92a7e0ad247c0c4b609809c2600547bc97999bcb7
boot=e42c1b09b4c4d3c91b1a3c5330619d37560287821994d269ba67a970069cd5fd
recovery=f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f
svc=android-hardware-media-c2-v4l2-hal-1-0
portrait=/sdcard/Movies/G997_HWDEC_SCREEN_640x1024_PRIVATE.mp4
landscape=/sdcard/Movies/G997_HWDEC_CAMERA_720P_PRIVATE.mp4
sha() { sha256sum "$1" | cut -d ' ' -f 1; }
bound() { awk -v f="$target" '$5==f {found=1;exit} END {exit !found}' /proc/self/mountinfo; }
read_cma() {
  echo "G998_CMA_SAMPLE stage=$1 sec=$(date +%s)"
  grep -E '^CmaFree:|^CmaTotal:|^MemAvailable:' /proc/meminfo
}
wait_hal() {
 j=0
 while [ "$(getprop init.svc.$svc)" != "$1" ] && [ "$j" -lt 15 ]; do
  sleep 1; j=$((j+1))
 done
 [ "$(getprop init.svc.$svc)" = "$1" ]
}
restore() {
 am force-stop com.android.gallery3d >/dev/null 2>&1 || true
 if bound; then
  setprop ctl.stop "$svc"
  wait_hal stopped || echo G998_HAL_STOP_DELAY
  umount "$target" || umount -l "$target" || { echo G998_UNMOUNT_FAILED; return 30; }
 fi
 [ "$(sha "$target")" = "$old" ] || { echo G998_G975_SHA_RESTORE_FAIL; return 31; }
 setprop ctl.start "$svc"
 wait_hal running || { echo G998_HAL_RESTART_FAIL; return 32; }
 [ "$(sha /system/vendor/lib/libril.so)" = "$radio" ] || { echo G998_RIL_DRIFT_BLOCK; return 33; }
 rm -f "$flag"
 echo G998_G975_RESTORED
}
case "$mode" in
 --restore) restore; exit $?;;
 --watchdog)
  sleep 135
  if [ -e "$flag" ] || bound; then
    restore >>"$result" 2>&1
  else
    echo G998_WATCHDOG_INERT >>"$result"
  fi
  exit $?;;
 --trial) ;;
 *) exit 2;;
esac
[ "$(id -u)" = 0 ] || exit 10
[ "$(getprop ro.product.device)" = lt013g ] || exit 11
[ "$(getprop ro.build.version.sdk)" = 30 ] || exit 12
[ "$(getprop sys.boot_completed)" = 1 ] || exit 13
[ "$(getenforce)" = Enforcing ] || exit 14
[ "$(getprop init.svc.$svc)" = running ] || exit 15
[ "$(sha "$target")" = "$old" ] || exit 16
[ "$(sha "$stage")" = "$new" ] || exit 17
[ "$(sha /system/vendor/lib/libril.so)" = "$radio" ] || exit 18
[ "$(sha /dev/block/mmcblk0p9)" = "$boot" ] || exit 19
[ "$(sha /dev/block/mmcblk0p10)" = "$recovery" ] || exit 20
[ "$(sha "$portrait")" = 4a29b7b8c0e916c03e7558f9075a21aab0016c7beb701e46773cea226c71404e ] || exit 21
[ "$(sha "$landscape")" = 3d9480fc7f28c8e99f92e96a8407d983bcab24e4a91a8351223e41ed5bcef890 ] || exit 22
[ "$(stat -c '%a' "$stage")" = 644 ] || exit 23
ls -Z "$stage" | grep -F 'u:object_r:vendor_file:s0' >/dev/null || exit 24
[ ! -e "$flag" ] || exit 25
bound && exit 26
am force-stop com.android.gallery3d >/dev/null 2>&1 || true
setsid sh "$0" --watchdog >/data/local/tmp/G998_decoder_watchdog.log 2>&1 </dev/null &
touch "$flag"
cleanup() { restore; }
trap 'cleanup' EXIT
read_cma before-trial >>"$telemetry"
mount -o bind "$stage" "$target"
[ "$(sha "$target")" = "$new" ] || exit 27
setprop ctl.restart "$svc"
wait_hal running || exit 28
echo G998_CANDIDATE_ACTIVE
logcat -b main -v threadtime -s V4L2Decoder:I V4L2DecodeComponent:I NuPlayerDecoder:I NuPlayer:E CCodec:W MediaCodec:E C2VdaBqBlockPool:E >"$logs" 2>&1 </dev/null &
logger_pid=$!
trap 'kill "$logger_pid" 2>/dev/null || true; restore' EXIT
for clip in portrait landscape; do
 if [ "$clip" = portrait ]; then path="$portrait"; seconds=39; else path="$landscape"; seconds=23; fi
 echo "G998_CLIP_START $clip"
 am start -W -n com.android.gallery3d/.app.MovieActivity -a android.intent.action.VIEW -d "file://$path" -t video/mp4 --grant-read-uri-permission >/dev/null
 n=0
 while [ "$n" -lt "$seconds" ]; do
  if [ $((n % 4)) -eq 0 ]; then read_cma "$clip:$n" >>"$telemetry"; fi
  sleep 1
  n=$((n+1))
 done
 echo "G998_CLIP_DONE $clip"
 am force-stop com.android.gallery3d >/dev/null 2>&1 || true
 sleep 2
done
read_cma after-playback >>"$telemetry"
echo G998_TRIAL_DONE
