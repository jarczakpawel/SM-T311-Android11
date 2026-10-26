#!/system/bin/sh
# G1136: permanently install physically accepted G1099 screenrecord, only one
# qualified SYSTEM file. TWRP only, SHA-guarded, no user-data backup/wipe.
# G1099 source: frameworks/av Git 978e53672e; build Soong arm32 SHA pinned.
# Usage: /sbin/sh SCRIPT --preflight|--apply
set -eu
MODE="${1:-}"
case "$MODE" in --preflight|--apply) ;; *) echo G1136_USAGE_PRECHECK_OR_APPLY; exit 2;; esac
[ "$(id -u)" = 0 ] && [ -x /sbin/twrp ] &&
    [ "$(getprop ro.twrp.version)" != "" ] &&
    [ "$(getprop ro.product.device)" = lt013g ] ||
    { echo G1136_REFUSE_NOT_TWRP_LT013G_ROOT; exit 3; }

BOOT=acc2a0579e0f4975da3b569941534f5b572ad9a7ae2bf4409fd88b5f633dd565
REC=f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f
OLD_SCREEN=0f816fa9055b0b1ad20e7db0ba45fd9aea73a3fc86936657fb884fda5f4f8851
G1099_SCREEN=f9e3669acd0c9d75e4c0f209513c63d8c33867172a995f068a73f00261437634
G1132_INIT=1d25625666709b2fb5dc6d7a856c630f30f37635f727b06dba4d4ecd10f68781
C2=f735ad9ed95bff53eb0f928476b61b27f9875e23465f3f196bfe5d24877dd7c9
RIL=4e6bb8a9465005a01f5bebd92a7e0ad247c0c4b609809c2600547bc97999bcb7
P9=/dev/block/mmcblk0p9
P10=/dev/block/mmcblk0p10
P20=/dev/block/mmcblk0p20
SRC=/data/local/tmp/G1099_screenrecord_soong
DST=/system/system/bin/screenrecord
sha() { sha256sum "$1" | awk '{print $1}'; }
[ "$(sha "$P9")" = "$BOOT" ] || { echo G1136_WRONG_G1130_BOOT; exit 4; }
[ "$(sha "$P10")" = "$REC" ] || { echo G1136_WRONG_RECOVERY; exit 5; }
[ -b "$P20" ] && [ -f "$SRC" ] || { echo G1136_MISSING_SOURCE_OR_SYSTEM; exit 6; }
[ "$(sha "$SRC")" = "$G1099_SCREEN" ] || { echo G1136_WRONG_ACCEPTED_G1099_ELF; exit 7; }
[ -f /data/local/tmp/G1099_screenrecord_soong ] || exit 8
mkdir -p /system
if grep -q ' /system ' /proc/mounts; then echo G1136_REFUSE_ALREADY_MOUNTED; exit 9; fi
mount -t ext4 -o ro,noload "$P20" /system || { echo G1136_MOUNT_RO_FAIL; exit 10; }
MOUNTED=1
TMP=""
cleanup() {
    if [ -n "$TMP" ] && [ -e "$TMP" ]; then rm -f "$TMP"; fi
    if [ "$MOUNTED" = 1 ]; then umount /system || echo G1136_WARN_UMOUNT_FAIL; fi
}
trap cleanup EXIT HUP INT TERM
[ "$(sha /system/system/vendor/etc/init/init.lt013g.debug.rc)" = "$G1132_INIT" ] ||
    { echo G1136_G1132_INIT_NOT_INSTALLED; exit 11; }
[ "$(sha /system/system/vendor/lib/libv4l2_codec2_components.so)" = "$C2" ] ||
    { echo G1136_WRONG_CODEC2; exit 12; }
[ "$(sha /system/system/vendor/lib/libril.so)" = "$RIL" ] ||
    { echo G1136_WRONG_RIL; exit 13; }
[ -f "$DST" ] || { echo G1136_MISSING_TARGET; exit 14; }
CURRENT="$(sha "$DST")"
if [ "$CURRENT" = "$G1099_SCREEN" ]; then
    echo "G1136_ALREADY_PERSISTENT=$CURRENT"; exit 0
fi
[ "$CURRENT" = "$OLD_SCREEN" ] ||
    { echo "G1136_REFUSE_UNEXPECTED_SCREENRECORD_SHA=$CURRENT"; exit 15; }
if [ "$MODE" = --preflight ]; then
    echo "G1136_PREFLIGHT_PASS stock=$CURRENT accepted=$G1099_SCREEN"
    exit 0
fi
umount /system || { echo G1136_RO_UMOUNT_FAIL; exit 16; }
MOUNTED=0
mount -t ext4 -o rw "$P20" /system || { echo G1136_MOUNT_RW_FAIL; exit 17; }
MOUNTED=1
[ "$(sha "$DST")" = "$OLD_SCREEN" ] || { echo G1136_TARGET_CHANGED_AFTER_REMOUNT; exit 18; }
TMP="${DST}.G1136.pending"
[ ! -e "$TMP" ] || { echo G1136_REFUSE_EXISTING_TMP; exit 19; }
cat "$SRC" > "$TMP" || { echo G1136_COPY_FAIL; exit 20; }
[ "$(sha "$TMP")" = "$G1099_SCREEN" ] || { echo G1136_COPY_SHA_FAIL; exit 21; }
chown 0:0 "$TMP"
chmod 0755 "$TMP"
# TWRP may return a failure from chcon even when ext4 creates the inode
# with the exact already-correct system_file label. Verify the final label
# rather than requiring an unnecessary relabel syscall.
if ! ls -Z "$TMP" | grep -Fq 'u:object_r:system_file:s0'; then
    chcon u:object_r:system_file:s0 "$TMP" || { echo G1136_CONTEXT_FAIL; exit 22; }
fi
ls -Z "$TMP" | grep -Fq 'u:object_r:system_file:s0' ||
    { echo G1136_CONTEXT_NOT_VERIFIED; exit 25; }
mv "$TMP" "$DST" || { echo G1136_RENAME_FAIL; exit 23; }
TMP=""
sync
[ "$(sha "$DST")" = "$G1099_SCREEN" ] ||
    { echo G1136_FINAL_SHA_FAIL; exit 24; }
echo "G1136_ACCEPTED_G1099_PERSISTENT_TWRP_WRITE_PASS sha=$G1099_SCREEN"
