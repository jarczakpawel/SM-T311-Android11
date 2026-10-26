#!/system/bin/sh
set -eu
[ "$(id -u)" = 0 ] || exit 10
[ "$(getprop ro.product.device)" = lt013g ] || exit 11
[ "$(getprop sys.boot_completed)" = 1 ] || exit 12
[ "$(getenforce)" = Enforcing ] || exit 13
[ "$(getprop init.svc.android-hardware-media-c2-v4l2-hal-1-0)" = running ] || exit 14
[ "$(sha256sum /dev/block/mmcblk0p9 | cut -d ' ' -f1)" = a143a4b5bcf3f5571b82c65cc47a91d00985fa684b7228c268ffd4b37e9c267b ] || exit 15
[ "$(sha256sum /dev/block/mmcblk0p10 | cut -d ' ' -f1)" = f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f ] || exit 16
[ "$(sha256sum /system/vendor/lib/libril.so | cut -d ' ' -f1)" = 4e6bb8a9465005a01f5bebd92a7e0ad247c0c4b609809c2600547bc97999bcb7 ] || exit 17
[ "$(sha256sum /system/vendor/lib/libv4l2_codec2_components.so | cut -d ' ' -f1)" = 6137f22dad0e0d7670020d50fb86266c7755b24dd546cb7ce3830a2bb89f75f6 ] || exit 18
[ "$(grep '^CmaTotal:' /proc/meminfo | awk '{print $2}')" = 163840 ] || exit 19
portrait=/sdcard/Movies/G1003_LONG_PORTRAIT_PRIVATE.mp4
landscape=/sdcard/Movies/G1003_CAMERA_720P_PRIVATE.mp4
[ "$(sha256sum "$portrait" | cut -d ' ' -f1)" = b08cc2208b4aa5daed698b5590a757184fc1e0f33ed967b8eca5497e739feb46 ] || exit 20
[ "$(sha256sum "$landscape" | cut -d ' ' -f1)" = 3d9480fc7f28c8e99f92e96a8407d983bcab24e4a91a8351223e41ed5bcef890 ] || exit 21
wm dismiss-keyguard
input keyevent KEYCODE_HOME
dumpsys window | grep -Fq isKeyguardShowing=false || exit 22
log=/data/local/tmp/G1006_DECODER_CMA_LOGCAT_PRIVATE.txt
am force-stop com.android.gallery3d >/dev/null 2>&1 || true
logcat -b main -v threadtime -s V4L2Decoder:I V4L2DecodeComponent:I NuPlayerDecoder:I NuPlayer:E CCodec:W MediaCodec:E C2VdaBqBlockPool:E >"$log" 2>&1 </dev/null &
logpid=$!
cleanup() {
  am force-stop com.android.gallery3d >/dev/null 2>&1 || true
  kill "$logpid" 2>/dev/null || true
}
trap cleanup EXIT
play() {
  label="$1"
  path="$2"
  duration="$3"
  echo "G1006_CLIP_BEGIN $label"
  am start -W -n com.android.gallery3d/.app.MovieActivity -a android.intent.action.VIEW -d "file://$path" -t video/mp4 --grant-read-uri-permission
  sleep 1
  dumpsys window | grep -E "mCurrentFocus=.*com.android.gallery3d" >/dev/null || exit 23
  n=0
  while [ "$n" -lt "$duration" ]; do
    if [ $((n % 5)) -eq 0 ]; then
      echo "G1006_SAMPLE $label $n $(date +%s)"
      grep -E '^CmaTotal:|^CmaFree:|^MemAvailable:' /proc/meminfo
    fi
    sleep 1
    n=$((n+1))
  done
  echo "G1006_CLIP_END $label"
  dumpsys media.metrics | grep -E -A 26 -B 3 'c2.v4l2.avc.decoder|android.media.mediaplayer' | tail -120 || true
  am force-stop com.android.gallery3d >/dev/null 2>&1 || true
  sleep 2
}
play portrait "$portrait" 50
play landscape "$landscape" 25
echo G1006_DONE
