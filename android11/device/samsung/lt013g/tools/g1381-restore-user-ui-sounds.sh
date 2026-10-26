#!/system/bin/sh
# SM-T311 / Android 11 — explicit opt-in user preference restoration.
# Does not modify audio stream volumes, DND, ringer mode, audio HAL or system files.
# Run on the tablet as: /system/bin/sh /data/local/tmp/g1381-restore-user-ui-sounds.sh
set -eu

[ "$(getprop ro.product.device)" = lt013g ] || { echo "G1381_WRONG_DEVICE" >&2; exit 21; }
[ "$(getprop ro.build.version.release)" = 11 ] || { echo "G1381_WRONG_ANDROID" >&2; exit 22; }
[ "$(getprop sys.boot_completed)" = 1 ] || { echo "G1381_BOOT_INCOMPLETE" >&2; exit 23; }

# Per-user SettingsProvider values persist in /data over non-wipe ROM updates.
# Neither output volume nor any ringtone/music/notification/alarm volume is changed.
settings put system sound_effects_enabled 1
settings put system lockscreen_sounds_enabled 1

[ "$(settings get system sound_effects_enabled)" = 1 ] || exit 24
[ "$(settings get system lockscreen_sounds_enabled)" = 1 ] || exit 25
echo "G1381_UI_SOUNDS_RESTORED_EFFECTS_1_LOCKSCREEN_1_VOLUMES_UNTOUCHED"
