#!/system/bin/sh
# G1119 physical comparison: replay same 48 original REAL Codec2 NV12
# through independent proven G1116 MMAP -> MFC. No HAL bind or UI actions.
set -eu
bin=/data/local/tmp/G1119_EXACT_C2_REPLAY_ARM32
raw=/data/local/tmp/G1119_EXACT_C2_PRIVATE.nv12
out=/data/local/tmp/G1119_EXACT_C2_REPLAY_PRIVATE.h264
log=/data/local/tmp/G1119_EXACT_C2_REPLAY_PRIVATE.log
receipt=/data/local/tmp/G1119_EXACT_C2_REPLAY_PRIVATE.receipt
sha(){ sha256sum "$1" | cut -d ' ' -f 1; }
[ "$(id -u)" = 0 ] || exit 10
[ "$(getprop ro.product.device)" = lt013g ] || exit 11
[ "$(getprop ro.build.version.sdk)" = 30 ] || exit 12
[ "$(getprop sys.boot_completed)" = 1 ] || exit 13
[ "$(getenforce)" = Enforcing ] || exit 14
[ "$(getprop init.svc.android-hardware-media-c2-v4l2-hal-1-0)" = running ] || exit 15
[ "$(getprop debug.t311.mfcv5_native)" = 0 ] || exit 16
[ "$(getprop debug.t311.mfcv5_capture)" = 0 ] || exit 17
[ "$(sha /dev/block/mmcblk0p9)" = e42c1b09b4c4d3c91b1a3c5330619d37560287821994d269ba67a970069cd5fd ] || exit 18
[ "$(sha /dev/block/mmcblk0p10)" = f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f ] || exit 19
[ "$(sha /system/vendor/lib/libv4l2_codec2_components.so)" = f735ad9ed95bff53eb0f928476b61b27f9875e23465f3f196bfe5d24877dd7c9 ] || exit 20
[ "$(sha /system/vendor/lib/libril.so)" = 4e6bb8a9465005a01f5bebd92a7e0ad247c0c4b609809c2600547bc97999bcb7 ] || exit 21
[ "$(sha "$bin")" = e2ad1363aeb8f0da31785dd51698f8e5e8cdc8e34878082cd321c1d0bddba73d ] || exit 22
[ "$(sha "$raw")" = 58fdfc359fe3557a97cbee640ef90415a45902bfc179f83e3d2852891c48741f ] || exit 23
[ "$(stat -c '%s' "$raw")" = 125831184 ] || exit 24
[ "$(settings get system sound_effects_enabled)" = 0 ] || exit 25
[ "$(settings get system lockscreen_sounds_enabled)" = 0 ] || exit 26
[ "$(cat /sys/module/s5p_mfc/parameters/debug)" = 0 ] || exit 27
[ ! -e "$out" ] && [ ! -e "$log" ] && [ ! -e "$receipt" ] || exit 28
[ "$(df -k /data | awk 'NR==2 {print $4}')" -ge 100000 ] || exit 29
set +e
"$bin" "$raw" "$out" > "$log" 2>&1
rc=$?
set -e
{
  echo "G1119_REPLAY_EXIT=$rc"
  sha256sum "$raw" "$bin" "$log"
  if [ -e "$out" ]; then sha256sum "$out"; ls -l "$out"; fi
  tail -n 15 "$log"
  echo "G1119_POST_C2_SHA=$(sha /system/vendor/lib/libv4l2_codec2_components.so)"
  echo "G1119_POST_SOUND_EFFECTS=$(settings get system sound_effects_enabled)"
  echo "G1119_POST_LOCK_SOUNDS=$(settings get system lockscreen_sounds_enabled)"
} > "$receipt"
cat "$receipt"
[ "$rc" -eq 0 ] && [ -s "$out" ]
