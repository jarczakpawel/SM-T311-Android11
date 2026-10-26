#!/system/bin/sh
# G895 SM-T311: reversible SystemUI DEX/runtime preflight without backups.
# Bind a signed candidate APK for a bounded interval; NEVER overwrite /system.
# Parent / is shared, Android app namespaces are slaves: bind events propagate.
# Watchdog independently restores physical old APK even if ADB disconnects.
set -eu
# G906: exactly two bounded holding periods; extended trial is reserved for
# an in-app recording test, whose UI is not ready during the initial 8s boot.
# Independent watchdog always fires at 66s; extended total at most ~8+48=56s before fixed 66s watchdog.
trial_hold="${G906_TRIAL_HOLD_SECONDS:-8}"
case "$trial_hold" in
  8|36|48) ;;
  *) echo G906_UNQUALIFIED_TEST_DURATION; exit 42;;
esac
mode="${1:---trial}"
base=/data/local/tmp
src="$base/G895_Rectangular_SystemUI_signed.apk"
target=/system/system_ext/priv-app/SystemUI/SystemUI.apk
flag="$base/G895_UI_BIND_ACTIVE"
old=51d4870efed53ec1f1198ecc3a535af1878074aecf27a8c40c271069990cddae
new=dfc2d99b790791d86893818924344c138ce397eaf44b2014bf9eca8aaa7ac31c
sha() { sha256sum "$1" | cut -d ' ' -f 1; }
is_bound() { grep -F " $target " /proc/self/mountinfo >/dev/null 2>&1; }
restore() {
  if is_bound; then
    # On Android11 the running SystemUI maps its APK as an executable DEX
    # backing file. Ordinary umount fails EBUSY while that process lives.
    # MNT_DETACH safely unlinks the transient mount from init/system_server
    # and all slave namespaces; the original APK on system was NEVER changed.
    # Then restart the persistent SystemUI process to discard its old mapping.
    umount -l "$target" || { echo G895_CRITICAL_BIND_DETACH_FAILED; return 30; }
    echo G895_BIND_LAZY_DETACH_PASS
  fi
  [ "$(sha "$target")" = "$old" ] || { echo G895_ORIGINAL_NOT_VISIBLE; return 31; }
  rm -f "$flag"
  if [ "${2:-restart}" = restart ]; then
    p="$(pidof com.android.systemui 2>/dev/null || true)"
    if [ -n "$p" ]; then kill -9 "$p" || true; fi
  fi
  echo G895_RESTORED_ORIGINAL_SYSTEMUI_SHA_PASS
}
case "$mode" in
  --restore) restore "$@"; exit $? ;;
  --watchdog)
    sleep 66
    if [ -e "$flag" ] || is_bound; then
      echo G895_WATCHDOG_AUTO_ROLLBACK
      restore --restore restart
    else
      echo G895_WATCHDOG_NO_ACTION_NEEDED
    fi
    exit $? ;;
  --trial) ;;
  *) echo G895_INVALID_MODE; exit 2;;
esac
[ "$(id -u)" = 0 ] || { echo G895_NEED_ROOT; exit 11; }
[ "$(getprop ro.product.device)" = lt013g ] || { echo G895_WRONG_DEVICE; exit 12; }
[ "$(getprop ro.build.version.sdk)" = 30 ] || { echo G895_WRONG_ANDROID; exit 13; }
[ "$(getprop sys.boot_completed)" = 1 ] || { echo G895_ANDROID_NOT_BOOTED; exit 14; }
[ "$(getenforce)" = Enforcing ] || { echo G895_NOT_ENFORCING; exit 15; }
[ "$(sha "$target")" = "$old" ] || { echo G895_OLD_SHA_NOT_MATCH; exit 16; }
[ "$(sha "$src")" = "$new" ] || { echo G895_NEW_SHA_NOT_MATCH; exit 17; }
[ "$(stat -c '%a' "$src")" = 644 ] || { echo G895_BAD_STAGE_MODE; exit 18; }
ls -Z "$src" | grep -F 'u:object_r:system_file:s0' >/dev/null || { echo G895_BAD_STAGE_LABEL; exit 19; }
is_bound && { echo G895_OTHER_BIND_EXISTS; exit 20; }
[ ! -e "$flag" ] || { echo G895_OTHER_TRIAL_EXISTS; exit 21; }
[ -n "$(pidof com.android.systemui)" ] || { echo G895_NO_OLD_SYSTEMUI; exit 22; }
# Detached watchdog launched before changing any system namespace state.
setsid sh "$0" --watchdog > "$base/G895_watchdog.log" 2>&1 </dev/null &
wd=$!
[ "$wd" -gt 0 ] || { echo G895_WATCHDOG_NOT_STARTED; exit 23; }
touch "$flag"
trap 'restore --restore restart' EXIT
mount -o bind "$src" "$target"
is_bound || { echo G895_BIND_NOT_VISIBLE; exit 24; }
[ "$(sha "$target")" = "$new" ] || { echo G895_BIND_SHA_FAIL; exit 25; }
echo G895_TRANSIENT_BIND_SHA_PASS
prev="$(pidof com.android.systemui)"
kill -9 "$prev"
sleep 8
now="$(pidof com.android.systemui 2>/dev/null || true)"
echo "G895_SYSTEMUI_PID_OLD=$prev NEW=$now"
[ -n "$now" ] && [ "$now" != "$prev" ] || { echo G895_SYSTEMUI_FAILED_TO_RESTART; exit 26; }
[ "$(sha "$target")" = "$new" ] || { echo G895_BIND_LOST; exit 27; }
[ "$(getprop sys.boot_completed)" = 1 ] || { echo G895_BOOT_STATE_LOST; exit 28; }
[ "$(getenforce)" = Enforcing ] || { echo G895_SELINUX_CHANGED; exit 29; }
echo G895_TRANSIENT_G885_UI_RUNNING_ENFORCING_PASS
echo "G906_TRIAL_HOLD_SECONDS=$trial_hold"
sleep "$trial_hold"
# Returning from the script MUST automatically restore old physical APK and
# trigger its process restart, regardless of this experiment's result.
exit 0
