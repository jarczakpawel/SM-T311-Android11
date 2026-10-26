#!/sbin/sh
# G1142: exactly four mutually matched SELinux release artifacts.
# --preflight is read-only. --apply stages all 4 with SHA/SELinux-label
# guards before replacing any; mixed OLD/NEW can be safely resumed in TWRP.
# No backup/restore, no whole-SYSTEM flash, no PIN, no sound.
set -eu
MODE="${1:-}"
case "$MODE" in --preflight|--apply) ;; *) echo G1142_USAGE_PRECHECK_OR_APPLY; exit 2 ;; esac
[ "$(id -u)" = 0 ] && [ -x /sbin/twrp ] &&
[ -n "$(getprop ro.twrp.version)" ] &&
[ "$(getprop ro.product.device)" = lt013g ] ||
{ echo G1142_REFUSE_NOT_LT013G_TWRP; exit 3; }
sha() { sha256sum "$1" | awk '{print $1}'; }
refuse() { echo "G1142_REFUSE_$1"; exit 7; }
P9=/dev/block/mmcblk0p9
P10=/dev/block/mmcblk0p10
P20=/dev/block/mmcblk0p20
BOOT=acc2a0579e0f4975da3b569941534f5b572ad9a7ae2bf4409fd88b5f633dd565
REC=f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f
INIT=1d25625666709b2fb5dc6d7a856c630f30f37635f727b06dba4d4ecd10f68781
UDC=32b5ae4bdab4fb21ead45b332620395dfdf36920d8b2293bbebf26c168116343
SCREEN=f9e3669acd0c9d75e4c0f209513c63d8c33867172a995f068a73f00261437634
RIL=4e6bb8a9465005a01f5bebd92a7e0ad247c0c4b609809c2600547bc97999bcb7
C2=f735ad9ed95bff53eb0f928476b61b27f9875e23465f3f196bfe5d24877dd7c9
PLAT=1cf3e726026475c7161857eaf7aa64fe2d7bd7d2817cdc5264aee7f998a0dd76
PRODUCT=38acb15d02d5ac0f2a2789602e9df950c380d2799b4bdb59394e4eeabdd3a662

A=/system/system/system_ext/etc/selinux/system_ext_sepolicy.cil
B=/system/system/system_ext/etc/selinux/system_ext_sepolicy_and_mapping.sha256
C=/system/system/vendor/etc/selinux/precompiled_sepolicy
D=/system/system/vendor/etc/selinux/precompiled_sepolicy.system_ext_sepolicy_and_mapping.sha256
SA=/data/local/tmp/G1142_system_ext_sepolicy.cil
SB=/data/local/tmp/G1142_system_ext_mapping.sha256
SC=/data/local/tmp/G1142_precompiled_sepolicy
SD=/data/local/tmp/G1142_precompiled_mapping.sha256
OA=d2e2dbd5cb6ce7a94895d071aa0368dd357a9dfa77ba1d0aa74c8916124b8fea
OB=a0297055621062cb37fbf7ef01c1dfbd54004709fd82354f5c575f34c5aca425
OC=4ee1dde32f6ea827e858af031080c8b39ac2f42ffd65665a4c9e835fbe75e586
OD=$OB
NA=5882b053b412b9207dbf87f1c7541e8d96630fe98ff89ef9e9d8f2921661fb1b
NB=19fa6e2522db7a2afdfafa656660f2a70411c69a1e56e86525b514226223248f
NC=dbe01ff5e4d94a5da8c71681804da85592ed1d6274a936ef45249bee0cfe20a1
ND=$NB

[ -b "$P20" ] && [ "$(sha "$P9")" = "$BOOT" ] &&
[ "$(sha "$P10")" = "$REC" ] || refuse_BOOT_RECOVERY_PARTITIONS
for f in "$SA" "$SB" "$SC" "$SD"; do [ -f "$f" ] || refuse_STAGING_MISSING; done
[ "$(sha "$SA")" = "$NA" ] && [ "$(sha "$SB")" = "$NB" ] &&
[ "$(sha "$SC")" = "$NC" ] && [ "$(sha "$SD")" = "$ND" ] ||
refuse_STAGING_SHA
[ "$(cat "$SB")" = "$(cat "$SD")" ] &&
[ "$(cat "$SB")" = 9d57cab36fb4a701e0141b9834532f9b65d56d4c6a1bb396be1d783a40c8c2dd ] ||
refuse_MISMATCHED_MAPPING_CONTENT
grep -Fxq '(allow system_server exported_camera_prop (file (read getattr map open)))' "$SA" &&
grep -Fxq '(allow platform_app exported_camera_prop (file (read getattr map open)))' "$SA" ||
refuse_MISSING_NARROW_CAMERA_GRANTS
[ ! -e "$A" ] || :  # TWRP /system intentionally unmounted at start
grep -q ' /system ' /proc/mounts && refuse_SYSTEM_ALREADY_MOUNTED
MOUNTED=0
clean() {
  for t in "$A.G1142.pending" "$B.G1142.pending" "$C.G1142.pending" "$D.G1142.pending"; do
    if [ -e "$t" ]; then rm -f "$t"; fi
  done
  if [ "$MOUNTED" = 1 ]; then umount /system || echo G1142_WARN_SYSTEM_MOUNTED; fi
}
trap clean EXIT HUP INT TERM
mkdir -p /system
mount -t ext4 -o ro,noload "$P20" /system || refuse_MOUNT_RO
MOUNTED=1

[ "$(sha /system/system/vendor/etc/init/init.lt013g.debug.rc)" = "$INIT" ] &&
[ "$(sha /system/system/etc/init/init.lt013g.udc-uevent.rc)" = "$UDC" ] &&
[ "$(sha /system/system/bin/screenrecord)" = "$SCREEN" ] &&
[ "$(sha /system/system/vendor/lib/libril.so)" = "$RIL" ] &&
[ "$(sha /system/system/vendor/lib/libv4l2_codec2_components.so)" = "$C2" ] ||
refuse_ANDROID_INVARIANTS
[ "$(sha /system/system/etc/selinux/plat_sepolicy_and_mapping.sha256)" = "$PLAT" ] &&
[ "$(sha /system/system/vendor/etc/selinux/precompiled_sepolicy.plat_sepolicy_and_mapping.sha256)" = "$PLAT" ] &&
[ "$(sha /system/system/vendor/etc/selinux/precompiled_sepolicy.product_sepolicy_and_mapping.sha256)" = "$PRODUCT" ] ||
refuse_PLATFORM_PRODUCT_POLICY_IDS
check_current() {
  existing="$(sha "$1")"
  [ "$existing" = "$2" ] || [ "$existing" = "$3" ] ||
  refuse_UNKNOWN_CURRENT_POLICY
}
check_current "$A" "$OA" "$NA"
check_current "$B" "$OB" "$NB"
check_current "$C" "$OC" "$NC"
check_current "$D" "$OD" "$ND"
if [ "$(sha "$A")" = "$NA" ] && [ "$(sha "$B")" = "$NB" ] &&
   [ "$(sha "$C")" = "$NC" ] && [ "$(sha "$D")" = "$ND" ]; then
  echo G1142_ALREADY_INSTALLED_EXACT_4; exit 0
fi
if [ "$MODE" = --preflight ]; then
  echo G1142_PREFLIGHT_4_FILES_PASS; exit 0
fi
umount /system || refuse_UNMOUNT_RO
MOUNTED=0
mount -t ext4 -o rw "$P20" /system || refuse_MOUNT_RW
MOUNTED=1
# Recheck after remount; refuse an unexpected concurrent mutation.
check_current "$A" "$OA" "$NA"
check_current "$B" "$OB" "$NB"
check_current "$C" "$OC" "$NC"
check_current "$D" "$OD" "$ND"
stage() {
  target="$1"; source="$2"; expected="$3"; label="$4"
  temp="$target.G1142.pending"
  [ ! -e "$temp" ] || refuse_FOREIGN_TEMP
  cat "$source" > "$temp" || refuse_STAGE_WRITE
  chown 0:0 "$temp"
  chmod 0644 "$temp"
  [ "$(sha "$temp")" = "$expected" ] || refuse_STAGE_SHA
  if ! ls -Z "$temp" | grep -Fq "u:object_r:$label:s0"; then
    chcon "u:object_r:$label:s0" "$temp" || refuse_SELINUX_LABEL_SET
  fi
  ls -Z "$temp" | grep -Fq "u:object_r:$label:s0" || refuse_SELINUX_LABEL_VERIFY
}
stage "$A" "$SA" "$NA" system_file
stage "$B" "$SB" "$NB" system_file
stage "$C" "$SC" "$NC" vendor_configs_file
stage "$D" "$SD" "$ND" vendor_configs_file

# All four files fully staged and checked BEFORE the first atomic rename.
mv "$A.G1142.pending" "$A" || refuse_RENAME_CIL
mv "$B.G1142.pending" "$B" || refuse_RENAME_SYS_EXT_ID
mv "$C.G1142.pending" "$C" || refuse_RENAME_COMPILED_POLICY
mv "$D.G1142.pending" "$D" || refuse_RENAME_VENDOR_ID
sync
[ "$(sha "$A")" = "$NA" ] && [ "$(sha "$B")" = "$NB" ] &&
[ "$(sha "$C")" = "$NC" ] && [ "$(sha "$D")" = "$ND" ] ||
refuse_POSTWRITE_SHA
echo G1142_TWRP_EXACT_FOUR_POLICY_FILES_PASS
