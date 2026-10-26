#!/system/bin/sh
# G1400 — SM-T311 Android 11 targeted launcher dex AOT + faster visual UI.
# One-time user-space tuning only, reapply after Launcher APK replacement.
# Deliberately does NOT modify GPS/RIL/recording, HWUI renderer, kernel,
# LMKD/zRAM, ro.config.low_ram, stream sound volumes or installed APK data.
set -eu
mode="${1:---audit}"
[ "$mode" = "--audit" ] || [ "$mode" = "--apply" ] || exit 20
[ "$(getprop ro.product.device)" = lt013g ] || exit 21
[ "$(getprop ro.build.version.release)" = 11 ] || exit 22
[ "$(getprop sys.boot_completed)" = 1 ] || exit 23
[ "$(getprop debug.lt013g.gps.v357.lto)" = 0 ] || exit 24

if [ "$mode" = "--apply" ]; then
    # Android11 ART official speed filter: compile an existing user-installed
    # Launcher3 update; system SystemUI is ALREADY [status=speed].
    # Force compiles bytecode but never clears app data or app preferences.
    result="$(cmd package compile -m speed -f com.android.launcher3)"
    printf '%s
' "$result"
    [ "$result" = Success ] || exit 25

    # Shorter animations change only presentation latency, NOT actual GPU fps.
    settings put global window_animation_scale 0.5
    settings put global transition_animation_scale 0.5
    settings put global animator_duration_scale 0.5
fi
printf 'G1400_WINDOW_ANIM=%s
' "$(settings get global window_animation_scale)"
printf 'G1400_TRANSITION_ANIM=%s
' "$(settings get global transition_animation_scale)"
printf 'G1400_ANIMATOR_ANIM=%s
' "$(settings get global animator_duration_scale)"
printf 'G1400_SOUNDS=%s/%s
' "$(settings get system sound_effects_enabled)" "$(settings get system lockscreen_sounds_enabled)"
printf 'G1400_GNSS_LTO=%s
' "$(getprop debug.lt013g.gps.v357.lto)"
if [ "$mode" = "--apply" ]; then
    [ "$(settings get global window_animation_scale)" = 0.5 ] || exit 26
    [ "$(settings get global transition_animation_scale)" = 0.5 ] || exit 27
    [ "$(settings get global animator_duration_scale)" = 0.5 ] || exit 28
    [ "$(settings get system sound_effects_enabled)" = 1 ] || exit 29
    [ "$(settings get system lockscreen_sounds_enabled)" = 1 ] || exit 30
    echo G1400_LAUNCHER_AOT_AND_FAST_UI_SETTINGS_PASS
else
    echo G1400_READONLY_AUDIT_PASS
fi
