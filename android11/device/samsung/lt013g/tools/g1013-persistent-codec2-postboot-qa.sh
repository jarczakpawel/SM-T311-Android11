#!/system/bin/sh
set -eu
log=/data/local/tmp/G1013_PERSISTENT_CODEC2_LOGCAT_PRIVATE.txt
metrics=/data/local/tmp/G1013_PERSISTENT_CODEC2_METRICS_PRIVATE.txt
video=/sdcard/Movies/G1003_LONG_PORTRAIT_PRIVATE.mp4
sha() { sha256sum "$1" | cut -d ' ' -f1; }
[ "$(id -u)" = 0 ] || exit 10
[ "$(getprop ro.product.device)" = lt013g ] || exit 11
[ "$(getprop sys.boot_completed)" = 1 ] || exit 12
[ "$(getenforce)" = Enforcing ] || exit 13
[ "$(sha /system/vendor/lib/libv4l2_codec2_components.so)" = f735ad9ed95bff53eb0f928476b61b27f9875e23465f3f196bfe5d24877dd7c9 ] || exit 14
[ "$(sha /system/vendor/lib/libril.so)" = 4e6bb8a9465005a01f5bebd92a7e0ad247c0c4b609809c2600547bc97999bcb7 ] || exit 15
[ "$(sha /dev/block/mmcblk0p9)" = e42c1b09b4c4d3c91b1a3c5330619d37560287821994d269ba67a970069cd5fd ] || exit 16
[ "$(sha /dev/block/mmcblk0p10)" = f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f ] || exit 17
[ "$(sha "$video")" = b08cc2208b4aa5daed698b5590a757184fc1e0f33ed967b8eca5497e739feb46 ] || exit 18
[ "$(getprop init.svc.android-hardware-media-c2-v4l2-hal-1-0)" = running ] || exit 19
wm dismiss-keyguard
input keyevent KEYCODE_HOME
am force-stop com.android.gallery3d >/dev/null 2>&1 || true
logcat -b main -v threadtime -T 1 -s V4L2Decoder:I V4L2DecodeComponent:I VideoFramePool:W C2VdaBqBlockPool:W CCodec:W NuPlayerDecoder:E NuPlayer:E >"$log" 2>&1 </dev/null &
logger_pid=$!
cleanup() {
 kill "$logger_pid" 2>/dev/null || true
 am force-stop com.android.gallery3d >/dev/null 2>&1 || true
 input keyevent KEYCODE_HOME >/dev/null 2>&1 || true
}
trap cleanup EXIT
echo G1013_BEGIN
am start -W -n com.android.gallery3d/.app.MovieActivity -a android.intent.action.VIEW -d "file://$video" -t video/mp4 --grant-read-uri-permission
sleep 2
dumpsys window | grep -E 'mCurrentFocus=.*com.android.gallery3d' >/dev/null || exit 20
echo G1013_GALLERY_FOCUSED
sleep 18
echo G1013_18S_PLAYBACK_ELAPSED
grep -E '^CmaTotal:|^CmaFree:' /proc/meminfo
am force-stop com.android.gallery3d >/dev/null 2>&1 || true
sleep 1
dumpsys media.metrics >"$metrics" 2>&1
echo G1013_METRICS_SAVED
echo G1013_CODEC_SHA="$(sha /system/vendor/lib/libv4l2_codec2_components.so)"
echo G1013_DONE
