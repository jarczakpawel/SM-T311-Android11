#!/sbin/sh
# G979 SM-T311: guarded SystemUI G908 rotation-only three-file TWRP.
# Safety: no modem, no PIN/PUK/CP/EFS, no BOOT, no RECOVERY, no DATA wipes.
set -eu
mode="${1:---verify}"
case "$mode" in --verify|--install) ;; *) exit 2;; esac
sha() { sha256sum "$1" | cut -d ' ' -f 1; }
check() { [ "$(sha "$2")" = "$1" ] || { echo "G979_SHA_BLOCK $2"; exit 30; }; }
B=e42c1b09b4c4d3c91b1a3c5330619d37560287821994d269ba67a970069cd5fd
R=f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f
RADIO=4e6bb8a9465005a01f5bebd92a7e0ad247c0c4b609809c2600547bc97999bcb7
CODEC=6137f22dad0e0d7670020d50fb86266c7755b24dd546cb7ce3830a2bb89f75f6
AUDIO=284657375ed169fa12778ceb4c78fd71c18310b8f6d42c8eddf429c61232a820
APKO=51d4870efed53ec1f1198ecc3a535af1878074aecf27a8c40c271069990cddae
APKN=dfc2d99b790791d86893818924344c138ce397eaf44b2014bf9eca8aaa7ac31c
ODEXO=dba8003434815b7bd98d3edca94cfe3a9d947786f8564c88eff2dfd3bf2c047a
ODEXN=f1bf8b37413cd5774b9a226064d3dc60702c855bc152ceada613c36ef3871416
VDEXO=ab056cb5f46206d6bb58b7b0e251ae0fc2c20ab2d6fb7b01002f6adb2f73b89d
VDEXN=d8ab9900ac62253c16ecab78d10a7b75341d05328f0fa7b0dda1eb0012e78720
APP=/system/system/system_ext/priv-app/SystemUI
A=$APP/SystemUI.apk
O=$APP/oat/arm/SystemUI.odex
V=$APP/oat/arm/SystemUI.vdex
SA=/data/local/tmp/G979_SystemUI.apk
SO=/data/local/tmp/G979_SystemUI.odex
SV=/data/local/tmp/G979_SystemUI.vdex
TA=$APP/.g979-stage.apk
TO=$APP/oat/arm/.g979-stage.odex
TV=$APP/oat/arm/.g979-stage.vdex
[ "$(id -u)" = 0 ] || exit 10
[ "$(getprop ro.twrp.version)" = 3.1.1-0 ] || exit 11
[ "$(getprop ro.product.device)" = lt013g ] || exit 12
grep -q '^/dev/block/mmcblk0p20 /system ext4 ' /proc/mounts || exit 13
check "$B" /dev/block/mmcblk0p9
check "$R" /dev/block/mmcblk0p10
check "$RADIO" /system/system/vendor/lib/libril.so
check "$CODEC" /system/system/vendor/lib/libv4l2_codec2_components.so
check "$AUDIO" /system/system/vendor/lib/hw/audio.primary.lt013g.so
check "$APKO" "$A"
check "$ODEXO" "$O"
check "$VDEXO" "$V"
check "$APKN" "$SA"
check "$ODEXN" "$SO"
check "$VDEXN" "$SV"
for f in "$TA" "$TO" "$TV"; do [ ! -e "$f" ] || exit 19; done
echo G979_STRICT_MODEL_BOOT_RIL_CODEC_AUDIO_OLD_UI_AND_NEW_FILES_PASS
[ "$mode" = --install ] || exit 0
RW=0
cleanup() {
 rm -f "$TA" "$TO" "$TV"
 [ "$RW" != 1 ] || mount -o remount,ro /system || true
}
trap cleanup EXIT
mount -o remount,rw /system
RW=1
cp "$SA" "$TA"; cp "$SO" "$TO"; cp "$SV" "$TV"
chown 0:0 "$TA" "$TO" "$TV"
chmod 0644 "$TA" "$TO" "$TV"
# TWRP 3.1.1 on this ext4 auto-labels new /system files correctly,
# but its chcon returns RC=1 even when the existing label is system_file.
# Verify effective label first; only attempt chcon if truly missing.
for f in "$TA" "$TO" "$TV"; do
 if ! ls -Z "$f" | grep -F 'u:object_r:system_file:s0' >/dev/null; then
   chcon u:object_r:system_file:s0 "$f" || true
 fi
 ls -Z "$f" | grep -F 'u:object_r:system_file:s0' >/dev/null || {
   echo "G979_BAD_SELINUX_LABEL $f"; exit 34;
 }
done
check "$APKN" "$TA"; check "$ODEXN" "$TO"; check "$VDEXN" "$TV"
sync
mv "$TA" "$A"; mv "$TO" "$O"; mv "$TV" "$V"
sync
check "$APKN" "$A"; check "$ODEXN" "$O"; check "$VDEXN" "$V"
mount -o remount,ro /system
RW=0
check "$B" /dev/block/mmcblk0p9
check "$R" /dev/block/mmcblk0p10
check "$RADIO" /system/system/vendor/lib/libril.so
check "$CODEC" /system/system/vendor/lib/libv4l2_codec2_components.so
check "$AUDIO" /system/system/vendor/lib/hw/audio.primary.lt013g.so
echo G979_SYSTEMUI_G908_THREE_FILE_ONLY_INSTALL_PASS
