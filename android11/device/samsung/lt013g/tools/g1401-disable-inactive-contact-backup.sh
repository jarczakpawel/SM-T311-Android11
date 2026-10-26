#!/system/bin/sh
# G1401 — optional RAM trim on Galaxy Tab 3 SM-T311 / Android 11.
# Only disable LocalContactsBackup (backup transport) when backup is OFF.
# No deletion of contacts, no pm clear/uninstall, no telephony/SIM change.
set -eu
mode="${1:---audit}"
[ "$mode" = "--audit" ] || [ "$mode" = "--apply" ] || exit 20
[ "$(getprop ro.product.device)" = lt013g ] || exit 21
[ "$(getprop ro.build.version.release)" = 11 ] || exit 22
[ "$(getprop sys.boot_completed)" = 1 ] || exit 23
[ "$(settings get secure backup_enabled)" = 0 ] || {
 echo "G1401_BACKUP_ENABLED_REFUSE" >&2; exit 24;
}
pkg=org.calyxos.backup.contacts
pm path "$pkg" |grep -Fq 'LocalContactsBackup.apk' || exit 25
if [ "$mode" = "--apply" ]; then
    pm disable-user --user 0 "$pkg"
fi
printf 'G1401_PACKAGE=%s
' "$pkg"
printf 'G1401_BACKUP_ENABLED=%s
' "$(settings get secure backup_enabled)"
printf 'G1401_PHONE_SIM=%s
' "$(getprop gsm.sim.state)"
printf 'G1401_LTO=%s
' "$(getprop debug.lt013g.gps.v357.lto)"
printf 'G1401_SOUNDS=%s/%s
' "$(settings get system sound_effects_enabled)" "$(settings get system lockscreen_sounds_enabled)"
if [ "$mode" = "--apply" ]; then
    pm list packages -d |grep -Fxq "package:$pkg" || exit 26
    [ "$(getprop gsm.sim.state)" = LOADED ] || exit 27
    [ "$(getprop debug.lt013g.gps.v357.lto)" = 0 ] || exit 28
    echo G1401_INACTIVE_CONTACT_BACKUP_DISABLED_DATA_INTACT_PASS
else
    echo G1401_READONLY_AUDIT_PASS
fi
