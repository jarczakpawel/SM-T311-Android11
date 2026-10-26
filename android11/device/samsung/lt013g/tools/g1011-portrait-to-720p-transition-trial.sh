#!/system/bin/sh
set -eu
mode="${1:---trial}"
target=/system/vendor/lib/libv4l2_codec2_components.so
stage=/data/local/tmp/G1002_decoder_trial.so
flag=/data/local/tmp/G1011_DECODER_TRIAL_ACTIVE
result=/data/local/tmp/G1011_SESSION_PRIVATE.txt
logs=/data/local/tmp/G1011_SESSION_LOGCAT_PRIVATE.txt
metrics=/data/local/tmp/G1011_SESSION_METRICS_PRIVATE.txt
old=6137f22dad0e0d7670020d50fb86266c7755b24dd546cb7ce3830a2bb89f75f6
new=f735ad9ed95bff53eb0f928476b61b27f9875e23465f3f196bfe5d24877dd7c9
radio=4e6bb8a9465005a01f5bebd92a7e0ad247c0c4b609809c2600547bc97999bcb7
boot=e42c1b09b4c4d3c91b1a3c5330619d37560287821994d269ba67a970069cd5fd
recovery=f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f
portrait=/sdcard/Movies/G1003_LONG_PORTRAIT_PRIVATE.mp4
landscape=/sdcard/Movies/G1003_CAMERA_720P_PRIVATE.mp4
portraithash=b08cc2208b4aa5daed698b5590a757184fc1e0f33ed967b8eca5497e739feb46
landscapehash=3d9480fc7f28c8e99f92e96a8407d983bcab24e4a91a8351223e41ed5bcef890
svc=android-hardware-media-c2-v4l2-hal-1-0
sha() { sha256sum "$1" | cut -d ' ' -f1; }
bound() { awk -v t="$target" '$5==t {found=1} END {exit !found}' /proc/self/mountinfo; }
wait_svc() {
 n=0
 while [ "$n" -lt 20 ]; do
  [ "$(getprop init.svc.$svc)" = "$1" ] && return 0
  sleep 1; n=$((n+1))
 done
 return 1
}
restore() {
 am force-stop com.android.gallery3d >/dev/null 2>&1 || true
 if bound; then
  setprop ctl.stop "$svc"
  wait_svc stopped || return 81
  umount "$target" || return 82
 fi
 [ "$(sha "$target")" = "$old" ] || return 83
 setprop ctl.start "$svc"
 wait_svc running || return 84
 [ "$(sha /system/vendor/lib/libril.so)" = "$radio" ] || return 85
 rm -f "$flag"
 echo G1011_G975_RESTORED
}
case "$mode" in
 --restore) restore; exit $?;;
 --watchdog)
  sleep 160
  if [ -e "$flag" ] || bound; then restore >>"$result" 2>&1; fi
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
[ "$(stat -c %a "$stage")" = 644 ] || exit 29
ls -Z "$stage" | grep -F u:object_r:vendor_file:s0 >/dev/null || exit 30
[ "$(sha /system/vendor/lib/libril.so)" = "$radio" ] || exit 18
[ "$(sha /dev/block/mmcblk0p9)" = "$boot" ] || exit 19
[ "$(sha /dev/block/mmcblk0p10)" = "$recovery" ] || exit 20
[ "$(sha "$portrait")" = "$portraithash" ] || exit 21
[ "$(sha "$landscape")" = "$landscapehash" ] || exit 22
[ ! -e "$flag" ] || exit 23
bound && exit 24
wm dismiss-keyguard
input keyevent KEYCODE_HOME
dumpsys window | grep -F 'isKeyguardShowing=false' >/dev/null || exit 25
setsid sh "$0" --watchdog >/data/local/tmp/G1011_WATCHDOG_PRIVATE.txt 2>&1 </dev/null &
touch "$flag"
trap restore EXIT
mount -o bind "$stage" "$target"
[ "$(sha "$target")" = "$new" ] || exit 26
setprop ctl.restart "$svc"
wait_svc running || exit 27
logcat -b main -v threadtime -T 1 -s V4L2Decoder:I V4L2DecodeComponent:I VideoFramePool:W C2VdaBqBlockPool:W CCodec:W NuPlayerDecoder:W NuPlayer:E MediaCodec:E >"$logs" 2>&1 </dev/null &
logpid=$!
trap 'kill "$logpid" 2>/dev/null || true; restore' EXIT
play() {
 label="$1"
 video="$2"
 duration="$3"
 echo "G1011_BEGIN $label $(date '+%m-%d %H:%M:%S')"
 am start -W -n com.android.gallery3d/.app.MovieActivity -a android.intent.action.VIEW -d "file://$video" -t video/mp4 --grant-read-uri-permission
 sleep 1
 dumpsys window | grep -E 'mCurrentFocus=.*com.android.gallery3d' >/dev/null || exit 28
 j=0
 while [ "$j" -lt "$duration" ]; do
  if [ $((j % 5)) -eq 0 ]; then
   echo "G1011_SAMPLE $label $j $(date +%s)"
   grep -E '^CmaTotal:|^CmaFree:|^MemAvailable:' /proc/meminfo
  fi
  sleep 1
  j=$((j+1))
 done
 echo "G1011_END $label $(date '+%m-%d %H:%M:%S')"
}
play portrait "$portrait" 35
play landscape "$landscape" 78
dumpsys media.metrics >"$metrics" 2>&1
echo G1011_COMPLETE_METRICS_SAVED
