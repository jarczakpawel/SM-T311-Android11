#!/sbin/sh
# G969 - SM-T311 TWRP 3.1.1-0 guarded, SINGLE Codec2 vendor library install.
# Never modify modem/SIM/PIN/PUK, BOOT, RECOVERY, other SYSTEM files or USERDATA.
# No backup, wipe, auto-reboot or PIN operation.
set -eu
MODE="${1:---verify}"
case "$MODE" in --verify|--install) ;; *) echo G969_BAD_MODE; exit 2;; esac
OLD=bd0a0b652c2835025121714b2683a78728d992ae9615ecb63c8037453b56de87
NEW=fa0edde02265287a5e29557a2354fd9aaddeb33c96c22538bf0f1c2bc164104f
BOOT=e42c1b09b4c4d3c91b1a3c5330619d37560287821994d269ba67a970069cd5fd
RECOVERY=f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f
SRC=/data/local/tmp/G967_codec2_g967.so
DEST=/system/system/vendor/lib/libv4l2_codec2_components.so
TMP=/system/system/vendor/lib/.libv4l2_codec2_components.so.g969-stage

sha(){ sha256sum "$1" | cut -d ' ' -f1; }
require_sha(){
 got="$(sha "$2")"
 [ "$got" = "$1" ] || { echo "G969_SHA_REJECT $2 got=$got expected=$1"; exit 16; }
}
[ "$(id -u)" = 0 ] || exit 10
[ "$(getprop ro.twrp.version)" = 3.1.1-0 ] || { echo G969_RECOVERY_REJECT; exit 11; }
[ "$(getprop ro.product.device)" = lt013g ] || { echo G969_MODEL_REJECT; exit 12; }
grep -q '^/dev/block/mmcblk0p20 /system ext4 ' /proc/mounts || {
 echo G969_SYSTEM_MOUNT_REJECT; exit 13;
}
require_sha "$BOOT" /dev/block/mmcblk0p9
require_sha "$RECOVERY" /dev/block/mmcblk0p10
require_sha "$NEW" "$SRC"
CURRENT="$(sha "$DEST")"
if [ "$CURRENT" = "$NEW" ]; then
 echo G969_ALREADY_INSTALLED_ALL_SHA_PASS
 exit 0
fi
[ "$CURRENT" = "$OLD" ] || { echo "G969_DESTINATION_REJECT_SHA=$CURRENT"; exit 17; }
[ ! -e "$TMP" ] || { echo G969_STALE_STAGE_REJECT; exit 18; }
echo G969_PRECHECK_MODEL_RECOVERY_BOOT_CODEC_SOURCE_PASS
[ "$MODE" = --install ] || exit 0

RW=0
cleanup(){
 if [ -e "$TMP" ]; then rm -f "$TMP"; fi
 if [ "$RW" = 1 ]; then
  mount -o remount,ro /system || echo G969_CRITICAL_SYSTEM_REMAINED_RW
 fi
}
trap 'cleanup' EXIT
mount -o remount,rw /system || { echo G969_REMOUNT_RW_FAIL; exit 21; }
RW=1
cp "$SRC" "$TMP"
chmod 0644 "$TMP"
chown 0:0 "$TMP"
chcon u:object_r:vendor_file:s0 "$TMP"
require_sha "$NEW" "$TMP"
sync
mv -f "$TMP" "$DEST"
sync
require_sha "$NEW" "$DEST"
mount -o remount,ro /system || { echo G969_REMOUNT_RO_FAIL; exit 22; }
RW=0
require_sha "$BOOT" /dev/block/mmcblk0p9
require_sha "$RECOVERY" /dev/block/mmcblk0p10
require_sha "$NEW" "$DEST"
echo G969_CODEC2_G967_ONLY_INSTALLED_SHA_PASS_NO_BACKUP_NO_MODEM
