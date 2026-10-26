#!/system/bin/sh
# G883: native Codec2 800x800 only periodic IDRs on SM-T311 Exynos MFC.
# The exact prior deployed binary is already available in host release tree,
# checked SHA 222ddd3c..., without taking any new device backup.
set -eu
src=/data/local/tmp/G883_libv4l2_codec2_components.so
dst=/vendor/lib/libv4l2_codec2_components.so
tmp=/vendor/lib/.libv4l2_codec2_components.so.G883-stage
old=222ddd3c6c2d94df32d24f3061313f81abf763717809e49eeb0c4b904d370a00
new=aca7e01590c15b256a5c10226cc532e2e12a1f7fb2924cda8fe44af2ffc7f320
fail() { echo "G883_BLOCKED:$*" >&2; exit 3; }
sha() { sha256sum "$1" | cut -d ' ' -f 1; }
[ "$(id -u)" = 0 ] || fail root_required
[ "$(getprop ro.product.device)" = lt013g ] || fail wrong_tablet
[ "$(getprop ro.build.version.sdk)" = 30 ] || fail wrong_android_version
[ "$(getprop sys.boot_completed)" = 1 ] || fail android_not_running
[ "$(getenforce)" = Enforcing ] || fail selinux_not_enforcing
[ "$(sha "$src")" = "$new" ] || fail invalid_staged_binary
[ "$(wc -c < "$src" | tr -d ' ')" = 362908 ] || fail unexpected_binary_size
if [ "$(sha "$dst")" = "$new" ]; then echo G883_ALREADY_INSTALLED; exit 0; fi
[ "$(sha "$dst")" = "$old" ] || fail unknown_current_codec
[ ! -e "$tmp" ] || fail stale_stage
[ "$(getprop init.svc.android-hardware-media-c2-v4l2-hal-1-0)" = running ] || fail codec_hal_not_running
mount -o remount,rw / || fail remount_rw
cleanup() { r=$?; [ ! -e "$tmp" ] || rm -f "$tmp"; mount -o remount,ro / || echo G883_CRITICAL_ROOT_REMAINED_RW >&2; exit "$r"; }
trap cleanup EXIT
cp "$src" "$tmp"
chown 0:0 "$tmp"
chmod 0644 "$tmp"
chcon u:object_r:vendor_file:s0 "$tmp"
[ "$(sha "$tmp")" = "$new" ] || fail stage_hash
mv -f "$tmp" "$dst"
sync
[ "$(sha "$dst")" = "$new" ] || fail installed_hash
[ "$(ls -Z "$dst" | grep -c 'u:object_r:vendor_file:s0')" = 1 ] || fail selinux_context
[ "$(stat -c '%a' "$dst")" = 644 ] || fail mode
# Restart just V4L2 Codec2 HAL before readonly remount: avoids EBUSY from
# old mapped ELF; do NOT reboot device, restart RIL, or touch ART/BOOT.
setprop ctl.restart android-hardware-media-c2-v4l2-hal-1-0
sleep 3
[ "$(getprop init.svc.android-hardware-media-c2-v4l2-hal-1-0)" = running ] || fail hal_restart
mount -o remount,ro / || fail remount_ro
trap - EXIT
echo G883_INSTALLED_ARM32_SHA_LABEL_MODE_RO_C2_HAL_RUNNING_PASS
