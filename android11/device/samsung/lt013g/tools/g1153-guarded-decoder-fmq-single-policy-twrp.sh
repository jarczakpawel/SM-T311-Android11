#!/sbin/sh
# G1153: only one precompiled SELinux binary; G1142 camera source and
# mapping hashes stay unchanged. No backups, PIN/PUK, sound, ROM or BOOT flash.
set -eu
MODE="${1:-}"
case "$MODE" in --preflight|--apply) ;; *) echo G1153_USAGE_PREFLIGHT_OR_APPLY; exit 2;; esac
refuse() { echo "G1153_REFUSE_$1"; exit 7; }
sha() { sha256sum "$1" | awk '{print $1}'; }
[ "$(id -u)" = 0 ] && [ -x /sbin/twrp ] &&
[ -n "$(getprop ro.twrp.version)" ] &&
[ "$(getprop ro.product.device)" = lt013g ] || refuse NOT_LT013G_TWRP
P9=/dev/block/mmcblk0p9
P10=/dev/block/mmcblk0p10
P20=/dev/block/mmcblk0p20
BOOT=acc2a0579e0f4975da3b569941534f5b572ad9a7ae2bf4409fd88b5f633dd565
REC=f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f
OLD=dbe01ff5e4d94a5da8c71681804da85592ed1d6274a936ef45249bee0cfe20a1
NEW=c56ffc6db7fc2fed5db8e1778ea0d8d7ae45f8214769e518c27e657d7a0a7bb9
PLAT=1cf3e726026475c7161857eaf7aa64fe2d7bd7d2817cdc5264aee7f998a0dd76
PRODUCT=38acb15d02d5ac0f2a2789602e9df950c380d2799b4bdb59394e4eeabdd3a662
EXT=19fa6e2522db7a2afdfafa656660f2a70411c69a1e56e86525b514226223248f
CAM=5882b053b412b9207dbf87f1c7541e8d96630fe98ff89ef9e9d8f2921661fb1b
RIL=4e6bb8a9465005a01f5bebd92a7e0ad247c0c4b609809c2600547bc97999bcb7
C2=f735ad9ed95bff53eb0f928476b61b27f9875e23465f3f196bfe5d24877dd7c9
INIT=1d25625666709b2fb5dc6d7a856c630f30f37635f727b06dba4d4ecd10f68781
UDC=32b5ae4bdab4fb21ead45b332620395dfdf36920d8b2293bbebf26c168116343
SRC=/data/local/tmp/G1153-source-canonical-FMQ-policy-ONLY.bin
DEST=/system/system/vendor/etc/selinux/precompiled_sepolicy
TMP="$DEST.G1153.pending"
[ -b "$P20" ] && [ "$(sha "$P9")" = "$BOOT" ] &&
[ "$(sha "$P10")" = "$REC" ] || refuse_WRONG_BOOT_RECOVERY_PARTITIONS
[ -f "$SRC" ] && [ ! -L "$SRC" ] && [ "$(sha "$SRC")" = "$NEW" ] ||
refuse_STAGE_BINARY_MISSING_OR_WRONG_SHA
grep -q ' /system ' /proc/mounts && refuse_SYSTEM_ALREADY_MOUNTED
MOUNTED=0
cleanup() {
  [ ! -e "$TMP" ] || rm -f "$TMP"
  if [ "$MOUNTED" = 1 ]; then umount /system || echo G1153_WARN_SYSTEM_MOUNTED; fi
}
trap cleanup EXIT HUP INT TERM
mkdir -p /system
mount -t ext4 -o ro,noload "$P20" /system || refuse_MOUNT_RO
MOUNTED=1
check_file() {
  [ -f "$1" ] && [ "$(sha "$1")" = "$2" ] || refuse_UNEXPECTED_SYSTEM_CONTENT
}
check_file /system/system/etc/selinux/plat_sepolicy_and_mapping.sha256 "$PLAT"
check_file /system/system/vendor/etc/selinux/precompiled_sepolicy.plat_sepolicy_and_mapping.sha256 "$PLAT"
check_file /system/system/vendor/etc/selinux/precompiled_sepolicy.product_sepolicy_and_mapping.sha256 "$PRODUCT"
check_file /system/system/vendor/etc/selinux/precompiled_sepolicy.system_ext_sepolicy_and_mapping.sha256 "$EXT"
check_file /system/system/system_ext/etc/selinux/system_ext_sepolicy_and_mapping.sha256 "$EXT"
check_file /system/system/system_ext/etc/selinux/system_ext_sepolicy.cil "$CAM"
check_file /system/system/vendor/lib/libril.so "$RIL"
check_file /system/system/vendor/lib/libv4l2_codec2_components.so "$C2"
check_file /system/system/vendor/etc/init/init.lt013g.debug.rc "$INIT"
check_file /system/system/etc/init/init.lt013g.udc-uevent.rc "$UDC"
CURRENT="$(sha "$DEST")"
[ "$CURRENT" = "$OLD" ] || [ "$CURRENT" = "$NEW" ] ||
refuse_UNRECOGNIZED_CURRENT_POLICY
if [ "$CURRENT" = "$NEW" ]; then
  echo G1153_POLICY_ALREADY_INSTALLED_EXACT; exit 0
fi
if [ "$MODE" = --preflight ]; then
  echo G1153_TWRP_PREFLIGHT_EXACT_ONE_BINARY_PASS; exit 0
fi
umount /system || refuse_UNMOUNT_RO
MOUNTED=0
mount -t ext4 -o rw "$P20" /system || refuse_MOUNT_RW
MOUNTED=1
[ "$(sha "$DEST")" = "$OLD" ] || refuse_POLICY_CHANGED_DURING_REMOUNT
[ ! -e "$TMP" ] && [ ! -L "$DEST" ] || refuse_FOREIGN_TEMP_OR_SYMLINK
cat "$SRC" > "$TMP" || refuse_STAGE_WRITE
chown 0:0 "$TMP"
chmod 0644 "$TMP"
[ "$(sha "$TMP")" = "$NEW" ] || refuse_STAGE_SHA
chcon u:object_r:vendor_configs_file:s0 "$TMP" || refuse_SET_LABEL
ls -Z "$TMP" | grep -Fq 'u:object_r:vendor_configs_file:s0' || refuse_LABEL
mv "$TMP" "$DEST" || refuse_ATOMIC_RENAME
sync
[ "$(sha "$DEST")" = "$NEW" ] || refuse_POSTWRITE_SHA
echo G1153_ONE_BINARY_TWRP_INSTALL_PASS_NO_BACKUP
