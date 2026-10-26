#!/system/bin/sh
# G1102: direct hardware MFCv5 native PRIME DMA-BUF sampled CPU NV12 checksums PRE QBUF and POST DQBUF
# static synthetic NV12 content, no user media/audio/flash/backup/reboot.
set -eu
bin=/data/local/tmp/G1102_EXYNOS_GEM_PRIME_MFC_ARM32
video=/data/local/tmp/G1102_EXYNOS_GEM_DMABUF_HARDWARE_PRIVATE.h264
log=/data/local/tmp/G1102_NATIVE_RUN_PRIVATE.log
result=/data/local/tmp/G1102_NATIVE_RESULT_PRIVATE.txt
sha(){ sha256sum "$1" | cut -d ' ' -f 1; }
[ "$(id -u)" = 0 ] || exit 10
[ "$(getprop ro.product.device)" = lt013g ] || exit 11
[ "$(getprop ro.build.version.sdk)" = 30 ] || exit 12
[ "$(getprop sys.boot_completed)" = 1 ] || exit 13
[ "$(getenforce)" = Enforcing ] || exit 14
[ "$(sha /dev/block/mmcblk0p9)" = e42c1b09b4c4d3c91b1a3c5330619d37560287821994d269ba67a970069cd5fd ] || exit 15
[ "$(sha /dev/block/mmcblk0p10)" = f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f ] || exit 16
[ "$(sha /system/vendor/lib/libril.so)" = 4e6bb8a9465005a01f5bebd92a7e0ad247c0c4b609809c2600547bc97999bcb7 ] || exit 17
[ "$(sha /system/vendor/lib/libv4l2_codec2_components.so)" = f735ad9ed95bff53eb0f928476b61b27f9875e23465f3f196bfe5d24877dd7c9 ] || exit 18
[ "$(sha /system/bin/screenrecord)" = f9e3669acd0c9d75e4c0f209513c63d8c33867172a995f068a73f00261437634 ] || exit 19
[ "$(getprop init.svc.android-hardware-media-c2-v4l2-hal-1-0)" = running ] || exit 20
[ "$(cat /sys/class/video4linux/video7/name)" = s5p-mfc-enc ] || exit 21
[ "$(settings get system sound_effects_enabled)" = 0 ] || exit 22
[ "$(settings get system lockscreen_sounds_enabled)" = 0 ] || exit 23
[ "$(sha "$bin")" = e9efffc07a51319bfe1a789dabb896ca88cd900b1ce8aa55aed411a1212a022a ] || exit 24
[ -c /dev/dri/renderD128 ] || exit 25
[ ! -e "$video" ] && [ ! -e "$log" ] && [ ! -e "$result" ] || exit 26
[ "$(df -k /data | awk 'NR==2 {print $4}')" -ge 100000 ] || exit 27
"$bin" --probe
ulimit -f 32768
set +e
"$bin" --run >"$log" 2>&1
rc=$?
set -e
{
 echo "G1102_NATIVE_MFC_GEM_DMABUF_EXIT=$rc"
 if [ -e "$video" ]; then sha256sum "$video"; ls -l "$video"; fi
 tail -n 17 "$log"
} >"$result"
cat "$result"
exit "$rc"
