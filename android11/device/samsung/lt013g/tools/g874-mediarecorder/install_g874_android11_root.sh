#!/system/bin/sh
# G874 SM-T311 ONLY: atomically install one compiled native recorder library.
# No backup, no data wipe, no BOOT, TWRP or policy changes.
# Staged by adb_push to /data/local/tmp; run only via root adbd on Android11.
set -eu
SRC=/data/local/tmp/G874_libmediaplayerservice_ARM32.so
TARGET=/system/lib/libmediaplayerservice.so
TEMP=/system/lib/.libmediaplayerservice.so.g874-stage
OLD=c4257e6759d2319b32e3ee91a1554c00291ad5bab2e0d685480658bac9aeef51
NEW=1cd0baef2736ac9f411c2deb3b57c554cf970ce0855ce955f1ecccbefac37ca5
fail() { echo "G874_BLOCKED $*" >&2; exit 1; }
sha() { sha256sum "$1" | cut -d ' ' -f1; }
[ "$(id -u)" = 0 ] || fail not_root
[ "$(getprop ro.product.device)" = lt013g ] || fail wrong_device
[ "$(getprop ro.build.version.sdk)" = 30 ] || fail wrong_android_sdk
[ "$(getprop sys.boot_completed)" = 1 ] || fail android_not_booted
[ "$(getenforce)" = Enforcing ] || fail selinux_not_enforcing
[ "$(sha "$SRC")" = "$NEW" ] || fail staged_sha_changed
[ "$(wc -c < "$SRC" | tr -d ' ')" = 687716 ] || fail staged_size_wrong
if [ "$(sha "$TARGET")" = "$NEW" ]; then
    echo G874_ALREADY_INSTALLED_SHA_PASS
    exit 0
fi
[ "$(sha "$TARGET")" = "$OLD" ] || fail original_unexpected_sha
[ ! -e "$TEMP" ] || fail existing_temp_file
# On this board the system ext4 root is mounted at '/', even though the
# target path begins /system. If remount is unsupported, FAIL rather than
# entering TWRP or forcing SELinux permissive.
mount -o remount,rw / || fail failed_remount_rw
cleanup() {
    status=$?
    if [ -e "$TEMP" ]; then rm -f "$TEMP"; fi
    if ! mount -o remount,ro /; then echo G874_CRITICAL_RESTORE_READONLY_FAILED >&2; exit 98; fi
    exit "$status"
}
trap cleanup EXIT
cp "$SRC" "$TEMP"
chown 0:0 "$TEMP"
chmod 0644 "$TEMP"
chcon u:object_r:system_lib_file:s0 "$TEMP"
[ "$(sha "$TEMP")" = "$NEW" ] || fail stage_sha_changed
# Atomic same-filesystem rename. Loaded old ELF inode stays mapped until the
# media service restarts; all new dlopen calls see the completed new ELF.
mv -f "$TEMP" "$TARGET"
sync
[ "$(sha "$TARGET")" = "$NEW" ] || fail post_rename_sha_changed
[ "$(ls -Z "$TARGET" | grep -c 'u:object_r:system_lib_file:s0')" = 1 ] || fail wrong_selinux_context
[ "$(stat -c %a "$TARGET")" = 644 ] || fail wrong_dac_mode
# Release the old loaded inode FIRST. On this Samsung ext4 root, a remount
# to read-only returns EBUSY while mediaserver still maps the replaced ELF.
# Earlier G874 physical install hit that; restarting media then remounting
# read-only succeeded immediately without any whole-device reboot.
setprop ctl.restart media
sleep 2
[ "$(getprop init.svc.media)" = running ] || fail media_service_did_not_restart
[ "$(sha "$TARGET")" = "$NEW" ] || fail post_restart_sha_changed
mount -o remount,ro / || fail failed_remount_ro
trap - EXIT
# No UI, Gallery, camera, SMS, audio playback or screenrecord stress tests.
echo G874_NATIVE_MEDIARECORDER_INSTALLED_SHA_OWNERSHIP_SELINUX_RO_PASS
