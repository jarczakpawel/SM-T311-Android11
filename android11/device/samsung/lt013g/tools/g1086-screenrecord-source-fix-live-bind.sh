#!/system/bin/sh
# G1086 - silent reversible LIVE bind of previously physically QA-verified
# G1081 screenrecord error-status fix. No partition writes, backup, flash,
# reboot, recordings, audio, changes to Codec2 or radio.
# On device reboot bind disappears (source fix is persistent in frameworks/av).
set -eu
mode="${1:---status}"
target=/system/bin/screenrecord
stage=/data/local/tmp/G1086_screenrecord_G1081_accepted
old=0f816fa9055b0b1ad20e7db0ba45fd9aea73a3fc86936657fb884fda5f4f8851
new=4bdbac4413ce8a2a816139e09c1546750793eac94299a71d51861a551891f3d6
c2=f735ad9ed95bff53eb0f928476b61b27f9875e23465f3f196bfe5d24877dd7c9
ril=4e6bb8a9465005a01f5bebd92a7e0ad247c0c4b609809c2600547bc97999bcb7
boot=e42c1b09b4c4d3c91b1a3c5330619d37560287821994d269ba67a970069cd5fd
recovery=f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f

sha() { sha256sum "$1" | cut -d ' ' -f 1; }
bound() { awk -v target="$target" '$5==target {hit=1} END{exit !hit}' /proc/self/mountinfo; }
quiet() {
  [ "$(settings get system sound_effects_enabled)" = 0 ] &&
  [ "$(settings get system lockscreen_sounds_enabled)" = 0 ]
}
preflight() {
  [ "$(id -u)" = 0 ] || { echo G1086_NOT_ROOT; return 10; }
  [ "$(getprop ro.product.device)" = lt013g ] || return 11
  [ "$(getprop ro.build.version.sdk)" = 30 ] || return 12
  [ "$(getprop sys.boot_completed)" = 1 ] || return 13
  [ "$(getenforce)" = Enforcing ] || return 14
  quiet || { echo G1086_SOUNDS_NOT_DISABLED; return 15; }
  [ "$(sha /dev/block/mmcblk0p9)" = "$boot" ] || return 16
  [ "$(sha /dev/block/mmcblk0p10)" = "$recovery" ] || return 17
  [ "$(sha /system/vendor/lib/libril.so)" = "$ril" ] || return 18
  [ "$(sha /system/vendor/lib/libv4l2_codec2_components.so)" = "$c2" ] || return 19
  [ "$(getprop init.svc.android-hardware-media-c2-v4l2-hal-1-0)" = running ] || return 20
}
case "$mode" in
  --status)
    preflight
    if bound; then
      [ "$(sha "$target")" = "$new" ] || { echo G1086_UNKNOWN_BIND_REFUSED; exit 21; }
      echo G1086_ACTIVE_VOLATILE_SCREENRECORD_PATCH
    else
      [ "$(sha "$target")" = "$old" ] || { echo G1086_UNEXPECTED_BASE; exit 22; }
      echo G1086_ORIGINAL_SCREENRECORD_UNMOUNTED
    fi
    ;;
  --enable)
    preflight
    [ -f "$stage" ] && [ "$(sha "$stage")" = "$new" ] || { echo G1086_BAD_STAGED_BINARY; exit 23; }
    [ "$(ls -Z "$stage" | awk '{print $1}')" = 'u:object_r:system_file:s0' ] || {
      echo G1086_STAGE_SELINUX_LABEL_INVALID; exit 24;
    }
    if bound; then
      [ "$(sha "$target")" = "$new" ] || { echo G1086_UNEXPECTED_EXISTING_MOUNT; exit 25; }
      echo G1086_ALREADY_ACTIVE; exit 0
    fi
    [ "$(sha "$target")" = "$old" ] || { echo G1086_ORIGINAL_SHA_MISMATCH; exit 26; }
    if ! mount -o bind "$stage" "$target"; then
      echo G1086_BIND_MOUNT_FAILED
      exit 27
    fi
    if [ "$(sha "$target")" != "$new" ] ||
       ! "$target" --help >/dev/null 2>&1; then
      echo G1086_VALIDATION_FAILED_ROLLING_BACK
      umount "$target" || { echo G1086_ROLLBACK_UNMOUNT_FAILED; exit 28; }
      [ "$(sha "$target")" = "$old" ] || exit 29
      exit 30
    fi
    quiet || { umount "$target"; echo G1086_SOUNDS_CHANGED_ROLLBACK; exit 31; }
    echo G1086_VOLATILE_LIVE_SCREENRECORD_PATCH_PASS
    ;;
  --disable)
    preflight
    if bound; then
      [ "$(sha "$target")" = "$new" ] || { echo G1086_UNKNOWN_BIND_WILL_NOT_UNMOUNT; exit 32; }
      umount "$target" || { echo G1086_UNMOUNT_FAILED; exit 33; }
    fi
    [ "$(sha "$target")" = "$old" ] || { echo G1086_RESTORE_SHA_FAILED; exit 34; }
    echo G1086_ORIGINAL_SCREENRECORD_RESTORED
    ;;
  *)
    echo 'G1086_USAGE --status|--enable|--disable'
    exit 2
    ;;
esac
