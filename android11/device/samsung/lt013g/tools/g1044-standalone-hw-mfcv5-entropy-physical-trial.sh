#!/system/bin/sh
# G1044: QUIET standalone high-entropy hardware MFCv5 V4L2 test, no screen, no sound.
# Does not read recordings from the device or change Android/kernel.
# Runs only the SHA-pinned 32-bit Bionic static userspace test on /dev/video7.
set -eu
device=/dev/video7
binary=/data/local/tmp/G1044_H264_STRESS_ARM32
video=/data/local/tmp/G1044_MFC_STRESS_PRIVATE.h264
log=/data/local/tmp/G1044_MFC_STRESS_LOG_PRIVATE.txt
result=/data/local/tmp/G1044_MFC_STRESS_RESULT_PRIVATE.txt
file_sha(){ sha256sum "$1" | cut -d ' ' -f1; }
[ "$(id -u)" = 0 ] || exit 11
[ "$(getprop ro.product.device)" = lt013g ] || exit 12
[ "$(getprop sys.boot_completed)" = 1 ] || exit 13
[ "$(getenforce)" = Enforcing ] || exit 14
[ "$(file_sha /dev/block/mmcblk0p9)" = e42c1b09b4c4d3c91b1a3c5330619d37560287821994d269ba67a970069cd5fd ] || exit 15
[ "$(file_sha /dev/block/mmcblk0p10)" = f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f ] || exit 16
[ "$(file_sha /system/vendor/lib/libv4l2_codec2_components.so)" = f735ad9ed95bff53eb0f928476b61b27f9875e23465f3f196bfe5d24877dd7c9 ] || exit 17
[ "$(file_sha /system/vendor/lib/libril.so)" = 4e6bb8a9465005a01f5bebd92a7e0ad247c0c4b609809c2600547bc97999bcb7 ] || exit 18
[ "$(file_sha "$binary")" = 874a49ad58ba6a4a63bcbef0b1a4feef4a2a88e0389845b05db602ddada2bbba ] || exit 19
[ "$(cat /sys/class/video4linux/video7/name)" = s5p-mfc-enc ] || exit 20
[ "$(getprop init.svc.android-hardware-media-c2-v4l2-hal-1-0)" = running ] || exit 21
[ ! -e "$video" ] && [ ! -e "$log" ] && [ ! -e "$result" ] || exit 22
[ "$(df -k /data | awk 'NR==2 {print $4}')" -ge 100000 ] || exit 23
# Limit the generated test bitstream to 64MiB, independent of MFC bitrate.
ulimit -f 65536
set +e
"$binary" --run-stress >"$log" 2>&1
rc=$?
set -e
{
 echo "G1044_HW_STANDALONE_EXIT=$rc"
 if [ -f "$video" ]; then
  sha256sum "$video"
  ls -l "$video"
 fi
 tail -12 "$log"
} >"$result"
cat "$result"
exit "$rc"
