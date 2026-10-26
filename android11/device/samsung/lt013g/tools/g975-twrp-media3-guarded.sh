#!/sbin/sh
# G975 - SM-T311 TWRP guarded THREE-file media-only install.
# Source-controlled, no backups, no wipes, no SIM/PIN/PUK/RIL/BOOT/RECOVERY changes.
set -eu
MODE="${1:---verify}"
case "$MODE" in --verify|--install) ;; *) echo G975_BAD_MODE; exit 2;; esac
B=e42c1b09b4c4d3c91b1a3c5330619d37560287821994d269ba67a970069cd5fd
R=f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f
RIL=4e6bb8a9465005a01f5bebd92a7e0ad247c0c4b609809c2600547bc97999bcb7
CODEC_OLD=fa0edde02265287a5e29557a2354fd9aaddeb33c96c22538bf0f1c2bc164104f
CODEC_NEW=6137f22dad0e0d7670020d50fb86266c7755b24dd546cb7ce3830a2bb89f75f6
AUDIO_OLD=636743a098acb4ab31bad39adb3c46875c461d4b313f5fddb6c2106318224cfa
AUDIO_NEW=284657375ed169fa12778ceb4c78fd71c18310b8f6d42c8eddf429c61232a820
POLICY_OLD=fc5c82a0b85c0b014ca746bbaa3173bff31772be6885c6cfa0095c59062ba97e
POLICY_NEW=df5800f2fd959bbd4787f22dce29714c895e6e5a51a362087fce535ee687b682

SRC_CODEC=/data/local/tmp/G975_CODEC2_MAIN_BASELINE.so
SRC_AUDIO=/data/local/tmp/G975_AUDIO_PRIMARY_48K.so
SRC_POLICY=/data/local/tmp/G975_AUDIO_POLICY_48K.xml
DST_CODEC=/system/system/vendor/lib/libv4l2_codec2_components.so
DST_AUDIO=/system/system/vendor/lib/hw/audio.primary.lt013g.so
DST_POLICY=/system/system/vendor/etc/audio_policy_configuration.xml
STAGE_CODEC=/system/system/vendor/lib/.g975-codec2-stage
STAGE_AUDIO=/system/system/vendor/lib/hw/.g975-audio-stage
STAGE_POLICY=/system/system/vendor/etc/.g975-policy-stage

sha(){ sha256sum "$1" | cut -d ' ' -f1; }
check(){ [ "$(sha "$2")" = "$1" ] || { echo "G975_SHA_REJECT $2 actual=$(sha "$2") expected=$1"; exit 16; }; }
accept_old_or_new(){
 actual="$(sha "$1")"
 [ "$actual" = "$2" ] || [ "$actual" = "$3" ] || {
    echo "G975_TARGET_CHANGED_UNEXPECTEDLY $1 sha=$actual"; exit 18;
 }
}
[ "$(id -u)" = 0 ] || exit 10
[ "$(getprop ro.twrp.version)" = 3.1.1-0 ] || { echo G975_RECOVERY_REJECT; exit 11; }
[ "$(getprop ro.product.device)" = lt013g ] || { echo G975_MODEL_REJECT; exit 12; }
grep -q '^/dev/block/mmcblk0p20 /system ext4 ' /proc/mounts || {
 echo G975_SYSTEM_MOUNT_REJECT; exit 13; }
check "$B" /dev/block/mmcblk0p9
check "$R" /dev/block/mmcblk0p10
check "$RIL" /system/system/vendor/lib/libril.so
check "$CODEC_NEW" "$SRC_CODEC"
check "$AUDIO_NEW" "$SRC_AUDIO"
check "$POLICY_NEW" "$SRC_POLICY"
accept_old_or_new "$DST_CODEC" "$CODEC_OLD" "$CODEC_NEW"
accept_old_or_new "$DST_AUDIO" "$AUDIO_OLD" "$AUDIO_NEW"
accept_old_or_new "$DST_POLICY" "$POLICY_OLD" "$POLICY_NEW"
for x in "$STAGE_CODEC" "$STAGE_AUDIO" "$STAGE_POLICY";do
 [ ! -e "$x" ] || { echo "G975_STALE_STAGE $x"; exit 19; }
done
echo G975_MEDIA_ONLY_PREFLIGHT_MODEL_BOOT_TWRP_RIL_ALL_3_FILES_PASS
[ "$MODE" = --install ] || exit 0
RW=0
cleanup(){
 rm -f "$STAGE_CODEC" "$STAGE_AUDIO" "$STAGE_POLICY"
 if [ "$RW" = 1 ];then mount -o remount,ro /system || echo G975_SYSTEM_RO_CRITICAL;fi
}
trap 'cleanup' EXIT
mount -o remount,rw /system || { echo G975_MOUNT_RW_FAIL; exit 21; }
RW=1
# Assemble all 3 destinations under the same ext4 before modifying a live path.
cp "$SRC_CODEC" "$STAGE_CODEC"
cp "$SRC_AUDIO" "$STAGE_AUDIO"
cp "$SRC_POLICY" "$STAGE_POLICY"
chmod 0644 "$STAGE_CODEC" "$STAGE_AUDIO" "$STAGE_POLICY"
chown 0:0 "$STAGE_CODEC" "$STAGE_AUDIO" "$STAGE_POLICY"
chcon u:object_r:vendor_file:s0 "$STAGE_CODEC" "$STAGE_AUDIO"
chcon u:object_r:vendor_configs_file:s0 "$STAGE_POLICY"
check "$CODEC_NEW" "$STAGE_CODEC"
check "$AUDIO_NEW" "$STAGE_AUDIO"
check "$POLICY_NEW" "$STAGE_POLICY"
sync
mv -f "$STAGE_CODEC" "$DST_CODEC"
mv -f "$STAGE_AUDIO" "$DST_AUDIO"
mv -f "$STAGE_POLICY" "$DST_POLICY"
sync
check "$CODEC_NEW" "$DST_CODEC"
check "$AUDIO_NEW" "$DST_AUDIO"
check "$POLICY_NEW" "$DST_POLICY"
mount -o remount,ro /system || { echo G975_MOUNT_RO_FAIL; exit 22; }
RW=0
check "$B" /dev/block/mmcblk0p9
check "$R" /dev/block/mmcblk0p10
check "$RIL" /system/system/vendor/lib/libril.so
echo G975_MEDIA3_ONLY_INSTALL_PASS_NO_BACKUP_NO_MODEM
