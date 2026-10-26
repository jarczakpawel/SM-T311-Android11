#!/sbin/sh
# G1141 exactly one existing SYSTEM init.rc action file via TWRP.
# --preflight: read-only; --apply: SHA-guarded atomic replace. No backup.
set -eu
MODE="${1:-}"
case "$MODE" in --preflight|--apply) ;; *) echo G1141_USAGE_PRECHECK_OR_APPLY; exit 2 ;; esac
[ "$(id -u)" = 0 ] && [ -x /sbin/twrp ] &&
[ -n "$(getprop ro.twrp.version)" ] &&
[ "$(getprop ro.product.device)" = lt013g ] ||
{ echo G1141_REFUSE_NOT_ROOT_LT013G_TWRP; exit 3; }

P9=/dev/block/mmcblk0p9
P10=/dev/block/mmcblk0p10
P20=/dev/block/mmcblk0p20
DST=/system/system/etc/init/init.lt013g.udc-uevent.rc
SRC=/data/local/tmp/G1141_RETIRE_UDC_DENIED_UEVENT.rc
OLD=58abbbe8dc5b3ba184bca4344f05117b848e43914e39a9b2f7cf6927b1fbc826
NEW=32b5ae4bdab4fb21ead45b332620395dfdf36920d8b2293bbebf26c168116343
BOOT=acc2a0579e0f4975da3b569941534f5b572ad9a7ae2bf4409fd88b5f633dd565
REC=f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f
G1135=1d25625666709b2fb5dc6d7a856c630f30f37635f727b06dba4d4ecd10f68781
G1099=f9e3669acd0c9d75e4c0f209513c63d8c33867172a995f068a73f00261437634
RIL=4e6bb8a9465005a01f5bebd92a7e0ad247c0c4b609809c2600547bc97999bcb7
C2=f735ad9ed95bff53eb0f928476b61b27f9875e23465f3f196bfe5d24877dd7c9
sha() { sha256sum "$1" | awk '{print $1}'; }
[ "$(sha "$P9")" = "$BOOT" ] || { echo G1141_WRONG_G1130_BOOT; exit 4; }
[ "$(sha "$P10")" = "$REC" ] || { echo G1141_WRONG_TWRP; exit 5; }
[ -b "$P20" ] && [ -f "$SRC" ] && [ "$(sha "$SRC")" = "$NEW" ] ||
{ echo G1141_STAGING_OR_PARTITION_MISSING; exit 6; }
if grep -q ' /system ' /proc/mounts; then echo G1141_REFUSE_MOUNT_ALREADY_ACTIVE; exit 7; fi
MOUNTED=0
TEMP=''
clean() {
 if [ -n "$TEMP" ] && [ -e "$TEMP" ]; then rm -f "$TEMP"; fi
 if [ "$MOUNTED" = 1 ]; then umount /system || echo G1141_WARN_MOUNT_STILL_ACTIVE; fi
}
trap clean EXIT HUP INT TERM
mkdir -p /system
mount -t ext4 -o ro,noload "$P20" /system || { echo G1141_MOUNT_RO_FAILED; exit 8; }
MOUNTED=1
[ "$(sha /system/system/vendor/etc/init/init.lt013g.debug.rc)" = "$G1135" ] &&
[ "$(sha /system/system/bin/screenrecord)" = "$G1099" ] &&
[ "$(sha /system/system/vendor/lib/libril.so)" = "$RIL" ] &&
[ "$(sha /system/system/vendor/lib/libv4l2_codec2_components.so)" = "$C2" ] ||
{ echo G1141_ACCEPTED_SYSTEM_INVARIANTS_FAILED; exit 9; }
[ -f "$DST" ] || { echo G1141_EXISTING_TARGET_ABSENT; exit 10; }
CURRENT="$(sha "$DST")"
if [ "$CURRENT" = "$NEW" ]; then
 echo "G1141_ALREADY_INSTALLED=$NEW"; exit 0
fi
[ "$CURRENT" = "$OLD" ] || { echo "G1141_UNKNOWN_PREVIOUS_RC=$CURRENT"; exit 11; }
if [ "$MODE" = --preflight ]; then
 echo "G1141_PREFLIGHT_PASS=$OLD next=$NEW"; exit 0
fi
umount /system || { echo G1141_UNMOUNT_RO_FAILED; exit 12; }
MOUNTED=0
mount -t ext4 -o rw "$P20" /system || { echo G1141_MOUNT_RW_FAILED; exit 13; }
MOUNTED=1
[ "$(sha "$DST")" = "$OLD" ] || { echo G1141_TARGET_CHANGED; exit 14; }
TEMP="${DST}.G1141.pending"
[ ! -e "$TEMP" ] || { echo G1141_REFUSE_FOREIGN_TMP; exit 15; }
cat "$SRC" > "$TEMP" || { echo G1141_COPY_FAILED; exit 16; }
[ "$(sha "$TEMP")" = "$NEW" ] || { echo G1141_TEMP_SHA_FAILED; exit 17; }
chown 0:0 "$TEMP"
chmod 0644 "$TEMP"
# ext4 new inode already inherits system_file; TWRP chcon often returns
# failure even when applying the identical label. Verify the actual label.
ls -Z "$TEMP" | grep -Fq 'u:object_r:system_file:s0' ||
{ echo G1141_SELABEL_MISMATCH; exit 18; }
mv "$TEMP" "$DST" || { echo G1141_ATOMIC_RENAME_FAILED; exit 19; }
TEMP=''
sync
[ "$(sha "$DST")" = "$NEW" ] ||
{ echo G1141_POSTWRITE_SHA_MISMATCH; exit 20; }
echo "G1141_TWRP_ONE_FILE_UDC_DENIED_WRITE_RETIRED_PASS=$NEW"
