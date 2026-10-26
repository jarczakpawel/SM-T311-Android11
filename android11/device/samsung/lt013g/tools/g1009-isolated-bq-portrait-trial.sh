#!/system/bin/sh
set -eu
mode="${1:---trial}"
target=/system/vendor/lib/libv4l2_codec2_components.so
stage=/data/local/tmp/G1009_codec2_trial.so
flag=/data/local/tmp/G1009_CODEC2_TRIAL_ACTIVE
result=/data/local/tmp/G1009_CODEC2_PHYSICAL_PRIVATE.txt
events=/data/local/tmp/G1009_BQ_LOGCAT_PRIVATE.txt
metrics=/data/local/tmp/G1009_MEDIA_METRICS_PRIVATE.txt
old=6137f22dad0e0d7670020d50fb86266c7755b24dd546cb7ce3830a2bb89f75f6
new=22728e96aec4bbb6bd192f5cf4d006006ddfcf1d80c9ea49a63e58930c24dbcc
radio=4e6bb8a9465005a01f5bebd92a7e0ad247c0c4b609809c2600547bc97999bcb7
boot=e42c1b09b4c4d3c91b1a3c5330619d37560287821994d269ba67a970069cd5fd
recovery=f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f
video=/sdcard/Movies/G1003_LONG_PORTRAIT_PRIVATE.mp4
video_sha=b08cc2208b4aa5daed698b5590a757184fc1e0f33ed967b8eca5497e739feb46
svc=android-hardware-media-c2-v4l2-hal-1-0
sha() { sha256sum "$1" | cut -d ' ' -f1; }
bound() { awk -v p="$target" '$5==p {f=1} END {exit !f}' /proc/self/mountinfo; }
wait_service() {
 n=0
 while [ "$n" -lt 18 ]; do
  [ "$(getprop init.svc.$svc)" = "$1" ] && return 0
  sleep 1
  n=$((n+1))
 done
 return 1
}
restore() {
 am force-stop com.android.gallery3d >/dev/null 2>&1 || true
 if bound; then
  setprop ctl.stop "$svc"
  wait_service stopped || return 81
  umount "$target" || return 82
 fi
 [ "$(sha "$target")" = "$old" ] || return 83
 setprop ctl.start "$svc"
 wait_service running || return 84
 [ "$(sha /system/vendor/lib/libril.so)" = "$radio" ] || return 85
 rm -f "$flag"
 echo G1009_G975_RESTORED
}
case "$mode" in
 --restore) restore; exit $? ;;
 --watchdog)
  sleep 85
  if [ -e "$flag" ] || bound; then restore >>"$result" 2>&1; fi
  exit $? ;;
 --trial) ;;
 *) exit 2 ;;
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
[ "$(sha "$video")" = "$video_sha" ] || exit 21
[ "$(stat -c '%a' "$stage")" = 644 ] || exit 22
ls -Z "$stage" | grep -F 'u:object_r:vendor_file:s0' >/dev/null || exit 23
[ ! -e "$flag" ] || exit 24
bound && exit 25
wm dismiss-keyguard
input keyevent KEYCODE_HOME
dumpsys window | grep -F 'isKeyguardShowing=false' >/dev/null || exit 26
setsid sh "$0" --watchdog >/data/local/tmp/G1009_WATCHDOG_PRIVATE.txt 2>&1 </dev/null &
touch "$flag"
trap 'restore' EXIT
mount -o bind "$stage" "$target"
[ "$(sha "$target")" = "$new" ] || exit 27
setprop ctl.restart "$svc"
wait_service running || exit 28
echo G1009_CANDIDATE_ACTIVE
logcat -b main -v threadtime -s VideoFramePool:W C2VdaBqBlockPool:W V4L2Decoder:I V4L2DecodeComponent:I NuPlayerDecoder:E NuPlayer:E CCodec:W MediaCodec:E >"$events" 2>&1 </dev/null &
logger_pid=$!
trap 'kill "$logger_pid" 2>/dev/null || true; restore' EXIT
echo G1009_PORTRAIT_BEGIN
am start -W -n com.android.gallery3d/.app.MovieActivity -a android.intent.action.VIEW -d "file://$video" -t video/mp4 --grant-read-uri-permission
sleep 1
dumpsys window | grep -E 'mCurrentFocus=.*com.android.gallery3d' >/dev/null || exit 29
i=0
while [ "$i" -lt 38 ]; do
 if [ $((i % 4)) -eq 0 ]; then
  echo "G1009_SAMPLE $i $(date +%s)"
  grep -E '^CmaTotal:|^CmaFree:|^MemAvailable:' /proc/meminfo
 fi
 sleep 1
 i=$((i+1))
done
dumpsys media.metrics >"$metrics" 2>&1
echo G1009_PORTRAIT_END
