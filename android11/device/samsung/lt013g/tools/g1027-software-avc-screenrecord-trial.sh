#!/system/bin/sh
# G1027: reversible test of the already registered Android 11 SOFTWARE AVC codec.
# Original installed screenrecord binary supports undocumented --codec-name.
# No SYSTEM/remount/module/firmware/BOOT changes, only private generated MP4.
set -eu
sha() { sha256sum "$1" | cut -d ' ' -f1; }
root=/data/local/tmp
video="$root/G1027_C2_SOFT_AVC_MOVING_PRIVATE.mp4"
stderr="$root/G1027_C2_SOFT_AVC_STDERR_PRIVATE.txt"
result="$root/G1027_C2_SOFT_AVC_RESULT_PRIVATE.txt"
[ "$(id -u)" = 0 ] || exit 11
[ "$(getprop ro.product.device)" = lt013g ] || exit 12
[ "$(getprop sys.boot_completed)" = 1 ] || exit 13
[ "$(getenforce)" = Enforcing ] || exit 14
[ "$(sha /dev/block/mmcblk0p9)" = e42c1b09b4c4d3c91b1a3c5330619d37560287821994d269ba67a970069cd5fd ] || exit 15
[ "$(sha /dev/block/mmcblk0p10)" = f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f ] || exit 16
[ "$(sha /system/vendor/lib/libv4l2_codec2_components.so)" = f735ad9ed95bff53eb0f928476b61b27f9875e23465f3f196bfe5d24877dd7c9 ] || exit 17
[ "$(sha /system/vendor/lib/libril.so)" = 4e6bb8a9465005a01f5bebd92a7e0ad247c0c4b609809c2600547bc97999bcb7 ] || exit 18
[ "$(sha /system/bin/screenrecord)" = 0f816fa9055b0b1ad20e7db0ba45fd9aea73a3fc86936657fb884fda5f4f8851 ] || exit 19
[ ! -e "$video" ] || exit 20
[ ! -e "$stderr" ] || exit 21
[ ! -e "$result" ] || exit 22
wm dismiss-keyguard >/dev/null 2>&1 || true
input keyevent KEYCODE_HOME
am start -a android.settings.SETTINGS >/dev/null 2>&1 || true
set +e
screenrecord --codec-name c2.android.avc.encoder --size 640x1024 \
  --bit-rate 1200000 --time-limit 18 "$video" >"$stderr" 2>&1 &
recorder=$!
(
  i=0
  while [ "$i" -lt 35 ]; do
    if [ $((i % 2)) -eq 0 ]; then input swipe 410 970 410 280 410 >/dev/null 2>&1 || true
    else input swipe 410 280 410 970 410 >/dev/null 2>&1 || true
    fi
    i=$((i+1))
  done
) &
swipes=$!
wait "$recorder"
rc=$?
kill "$swipes" 2>/dev/null || true
wait "$swipes" 2>/dev/null || true
set -e
input keyevent KEYCODE_HOME >/dev/null 2>&1 || true
echo "G1027_SCREENRECORD_EXIT=$rc" >"$result"
echo "G1027_CODEC_REQUESTED=c2.android.avc.encoder" >>"$result"
[ ! -e "$video" ] || sha256sum "$video" >>"$result"
[ ! -e "$video" ] || ls -l "$video" >>"$result"
cat "$result"
exit "$rc"
