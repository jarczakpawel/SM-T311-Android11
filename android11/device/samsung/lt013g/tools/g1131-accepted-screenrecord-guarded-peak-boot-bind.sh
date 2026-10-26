#!/system/bin/sh
# G1099 - verified ARM32 screenrecord EOS verification; VOLATILE bind only.
# Does not touch ROM, BOOT, TWRP, RIL, user data or sound settings.
# Reverts to previously physically accepted G1086 screenrecord on --disable.
set -eu
mode="${1:---status}"
target=/system/bin/screenrecord
stage=/data/local/tmp/G1099_screenrecord_soong
fallback=/data/local/tmp/G1086_screenrecord_G1081_accepted
new=f9e3669acd0c9d75e4c0f209513c63d8c33867172a995f068a73f00261437634
prior=4bdbac4413ce8a2a816139e09c1546750793eac94299a71d51861a551891f3d6
stock=0f816fa9055b0b1ad20e7db0ba45fd9aea73a3fc86936657fb884fda5f4f8851
c2=f735ad9ed95bff53eb0f928476b61b27f9875e23465f3f196bfe5d24877dd7c9
ril=4e6bb8a9465005a01f5bebd92a7e0ad247c0c4b609809c2600547bc97999bcb7
boot=478a9da10024c6ac6426b44ad779b0d6ae66c1355d9b09c2fbdd147ba8140d7f
recovery=f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f
sha() { sha256sum "$1" | cut -d ' ' -f 1; }
bound() { awk -v f="$target" '$5==f {x=1} END{exit !x}' /proc/self/mountinfo; }
quiet() {
  [ "$(settings get system sound_effects_enabled)" = 0 ] &&
  [ "$(settings get system lockscreen_sounds_enabled)" = 0 ]
}
preflight() {
  [ "$(id -u)" = 0 ] || return 10
  [ "$(getprop ro.product.device)" = lt013g ] || return 11
  [ "$(getprop ro.build.version.sdk)" = 30 ] || return 12
  [ "$(getprop sys.boot_completed)" = 1 ] || return 13
  [ "$(getenforce)" = Enforcing ] || return 14
  quiet || { echo G1099_SOUNDS_MUST_REMAIN_DISABLED; return 15; }
  [ "$(sha /dev/block/mmcblk0p9)" = "$boot" ] || return 16
  [ "$(sha /dev/block/mmcblk0p10)" = "$recovery" ] || return 17
  [ "$(sha /system/vendor/lib/libril.so)" = "$ril" ] || return 18
  [ "$(sha /system/vendor/lib/libv4l2_codec2_components.so)" = "$c2" ] || return 19
  [ "$(getprop init.svc.android-hardware-media-c2-v4l2-hal-1-0)" = running ] || return 20
}
source_guard() {
  [ -f "$stage" ] && [ "$(sha "$stage")" = "$new" ] || return 23
  [ "$(ls -Z "$stage" | awk '{print $1}')" = u:object_r:system_file:s0 ] || return 24
  [ -f "$fallback" ] && [ "$(sha "$fallback")" = "$prior" ] || return 25
}
restore_prior() {
  if bound; then
    [ "$(sha "$target")" = "$new" ] || return 31
    umount "$target" || return 32
  fi
  [ "$(sha "$target")" = "$stock" ] || return 33
  source_guard || return 34
  mount -o bind "$fallback" "$target" || return 35
  [ "$(sha "$target")" = "$prior" ] || return 36
  echo G1099_G1086_PREVIOUS_BIND_RESTORED
}
case "$mode" in
  --status)
    preflight
    if bound; then
      current="$(sha "$target")"
      if [ "$current" = "$new" ]; then
        echo G1099_ACTIVE_VOLATILE_SCREENRECORD_EOS_VERIFIED
      elif [ "$current" = "$prior" ]; then
        echo G1099_PREVIOUS_G1086_ACTIVE
      else
        echo G1099_UNEXPECTED_BIND_SHA; exit 41
      fi
    else
      [ "$(sha "$target")" = "$stock" ] || exit 42
      echo G1099_STOCK_SCREENRECORD_UNMOUNTED
    fi
    ;;
  --enable)
    preflight
    source_guard || { echo G1099_BAD_STAGED_OR_PRIOR_BINARY; exit 43; }
    if bound; then
      current="$(sha "$target")"
      [ "$current" = "$prior" ] || [ "$current" = "$new" ] || exit 44
      if [ "$current" = "$new" ]; then
        echo G1099_ALREADY_ACTIVE; exit 0
      fi
      # Unmount ONLY the exact prior verified G1086 bind.
      umount "$target" || exit 45
    fi
    if [ "$(sha "$target")" != "$stock" ]; then
      echo G1099_UNEXPECTED_STOCK_AFTER_UNMOUNT
      exit 46
    fi
    if ! mount -o bind "$stage" "$target"; then
      echo G1099_NEW_BIND_FAILED
      mount -o bind "$fallback" "$target" || exit 47
      exit 48
    fi
    if [ "$(sha "$target")" != "$new" ] ||
       ! "$target" --help >/dev/null 2>&1; then
      echo G1099_NEW_BINARY_CHECK_FAILED_RESTORE_PRIOR
      restore_prior || exit 49
      exit 50
    fi
    quiet || { restore_prior; exit 51; }
    echo G1099_VOLATILE_BIND_ACTIVE_SHA_VERIFIED
    ;;
  --disable)
    preflight
    if bound && [ "$(sha "$target")" = "$prior" ]; then
      echo G1099_G1086_ALREADY_ACTIVE; exit 0
    fi
    restore_prior
    ;;
  *)
    echo 'G1099_USAGE --status|--enable|--disable'
    exit 2
    ;;
esac
