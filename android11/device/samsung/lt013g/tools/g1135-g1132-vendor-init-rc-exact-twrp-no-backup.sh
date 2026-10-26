#!/system/bin/sh
# G1135: SHA-guarded single vendor init rc deployment via TWRP; no backups.
# Targets G1130 BOOT and the existing G1132 source commit 9f66a1a.
# Usage in TWRP: sh SCRIPT --preflight|--apply|--restore
set -eu

MODE="${1:-}"
case "$MODE" in --preflight|--apply|--restore) ;; *) echo "G1135_REFUSE_USAGE"; exit 2;; esac
[ "$(id -u)" = "0" ] || { echo "G1135_REFUSE_NOT_ROOT"; exit 3; }
[ -x /sbin/twrp ] || { echo "G1135_REFUSE_NOT_TWRP"; exit 4; }
[ -n "$(getprop ro.twrp.version)" ] || { echo "G1135_REFUSE_RECOVERY_PROPERTY"; exit 5; }
[ "$(getprop ro.product.device)" = "lt013g" ] || { echo "G1135_REFUSE_DEVICE"; exit 6; }

BOOT_EXPECT=acc2a0579e0f4975da3b569941534f5b572ad9a7ae2bf4409fd88b5f633dd565
RECOVERY_EXPECT=f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f
OLD_EXPECT=5286fb33d6841665b25e1363055703dad5b42063d61ff505a99432b375e7b5af
NEW_EXPECT=1d25625666709b2fb5dc6d7a856c630f30f37635f727b06dba4d4ecd10f68781
P9=/dev/block/mmcblk0p9
P10=/dev/block/mmcblk0p10
P20=/dev/block/mmcblk0p20
TARGET=/system/system/vendor/etc/init/init.lt013g.debug.rc

sha() { sha256sum "$1" | awk '{print $1}'; }
[ "$(sha "$P9")" = "$BOOT_EXPECT" ] || { echo "G1135_REFUSE_G1130_BOOT_SHA"; exit 7; }
[ "$(sha "$P10")" = "$RECOVERY_EXPECT" ] || { echo "G1135_REFUSE_RECOVERY_SHA"; exit 8; }
[ -b "$P20" ] || { echo "G1135_REFUSE_SYSTEM_BLOCK"; exit 9; }
mkdir -p /system
if grep -q ' /system ' /proc/mounts; then
    echo "G1135_REFUSE_SYSTEM_ALREADY_MOUNTED"
    exit 10
fi
mount -t ext4 -o ro,noload "$P20" /system || { echo "G1135_RO_MOUNT_FAIL"; exit 11; }
MOUNTED=1
TMP=""
cleanup() {
    if [ -n "$TMP" ] && [ -e "$TMP" ]; then rm -f "$TMP"; fi
    if [ "$MOUNTED" = 1 ]; then umount /system || echo "G1135_WARN_UMOUNT_FAILED"; fi
}
trap cleanup EXIT HUP INT TERM

[ -f "$TARGET" ] || { echo "G1135_REFUSE_TARGET_MISSING"; exit 12; }
CURRENT="$(sha "$TARGET")"
if [ "$MODE" = --restore ]; then
    EXPECT="$NEW_EXPECT"; DEST="$OLD_EXPECT"
else
    EXPECT="$OLD_EXPECT"; DEST="$NEW_EXPECT"
fi
if [ "$CURRENT" = "$DEST" ]; then
    echo "G1135_ALREADY_AT_DEST_SHA=$DEST"
    exit 0
fi
[ "$CURRENT" = "$EXPECT" ] || { echo "G1135_REFUSE_UNKNOWN_TARGET_SHA=$CURRENT"; exit 13; }
[ "$MODE" != --preflight ] || {
    echo "G1135_PREFLIGHT_PASS current=$CURRENT target=$NEW_EXPECT boot=$BOOT_EXPECT"
    exit 0
}
umount /system || { echo "G1135_RO_UMOUNT_FAIL"; exit 14; }
MOUNTED=0
mount -t ext4 -o rw "$P20" /system || { echo "G1135_RW_MOUNT_FAIL"; exit 15; }
MOUNTED=1
[ "$(sha "$TARGET")" = "$EXPECT" ] || { echo "G1135_REFUSE_TARGET_CHANGED_AFTER_REMOUNT"; exit 16; }
TMP="${TARGET}.G1135.pending"
[ ! -e "$TMP" ] || { echo "G1135_REFUSE_STALE_TEMP"; exit 17; }

if [ "$MODE" = "--restore" ]; then
    cat > "$TMP" <<'OLD_RC'
on early-init
    write /proc/sys/kernel/panic 5
    write /proc/sys/kernel/panic_on_oops 1

on boot
    write /sys/power/pm_print_times 0
    write /proc/sys/kernel/hung_task_timeout_secs 120

on property:sys.boot_completed=1
    write /proc/sys/kernel/softlockup_panic 1

service vendor.watchdogd /system/bin/watchdogd 5 10
    class core
    seclabel u:r:watchdogd:s0
OLD_RC
else
    cat > "$TMP" <<'NEW_RC'
on boot
    write /sys/power/pm_print_times 0

on property:sys.boot_completed=1
    write /proc/sys/kernel/softlockup_panic 1

service vendor.watchdogd /system/bin/watchdogd 5 10
    class core
    seclabel u:r:watchdogd:s0
NEW_RC
fi
[ "$(sha "$TMP")" = "$DEST" ] || { echo "G1135_REFUSE_PAYLOAD_SHA"; exit 18; }
chown 0:0 "$TMP"
chmod 0644 "$TMP"
chcon u:object_r:vendor_configs_file:s0 "$TMP" || { echo "G1135_REFUSE_CHCON"; exit 19; }
mv "$TMP" "$TARGET" || { echo "G1135_RENAME_FAIL"; exit 20; }
TMP=""
sync
[ "$(sha "$TARGET")" = "$DEST" ] || { echo "G1135_REFUSE_POSTWRITE_SHA"; exit 21; }
echo "G1135_TWRP_ONE_FILE_WRITE_PASS new_sha=$DEST mode=$MODE"
