#!/system/bin/sh
# G886: native Codec2 640x1024 and 1024x640 only periodic IDRs on SM-T311 Exynos MFC.
# This upgrades the SHA-attested on-device G883 Codec2 with no backup.
# No BOOT, ART, GPS, USB, RIL, gallery, or media files modified., without taking any new device backup.
set -eu
src=/data/local/tmp/G886_libv4l2_codec2_components.so
dst=/vendor/lib/libv4l2_codec2_components.so
tmp=/vendor/lib/.libv4l2_codec2_components.so.G886-stage
old=aca7e01590c15b256a5c10226cc532e2e12a1f7fb2924cda8fe44af2ffc7f320
new=bd0a0b652c2835025121714b2683a78728d992ae9615ecb63c8037453b56de87
fail() { echo "G886_BLOCKED:$*" >&2; exit 3; }
sha() { sha256sum "$1" | cut -d ' ' -f 1; }
[ "$(id -u)" = 0 ] || fail root_required
[ "$(getprop ro.product.device)" = lt013g ] || fail wrong_tablet
[ "$(getprop ro.build.version.sdk)" = 30 ] || fail wrong_android_version
[ "$(getprop sys.boot_completed)" = 1 ] || fail android_not_running
[ "$(getenforce)" = Enforcing ] || fail selinux_not_enforcing
[ "$(sha "$src")" = "$new" ] || fail invalid_staged_binary
[ "$(wc -c < "$src" | tr -d ' ')" = 362972 ] || fail unexpected_binary_size
if [ "$(sha "$dst")" = "$new" ]; then echo G886_ALREADY_INSTALLED; exit 0; fi
[ "$(sha "$dst")" = "$old" ] || fail unknown_current_codec
[ ! -e "$tmp" ] || fail stale_stage
[ "$(getprop init.svc.android-hardware-media-c2-v4l2-hal-1-0)" = running ] || fail codec_hal_not_running
mount -o remount,rw / || fail remount_rw
cleanup() { r=$?; [ ! -e "$tmp" ] || rm -f "$tmp"; mount -o remount,ro / || echo G886_CRITICAL_ROOT_REMAINED_RW >&2; exit "$r"; }
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
echo G886_RECTANGULAR_IDR_CODEC_SHA_LABEL_MODE_RO_C2_HAL_RUNNING_PASS
