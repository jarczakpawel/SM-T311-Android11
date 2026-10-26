#!/system/bin/sh
# SM-T311 G621: passive acceptance after guarded one-file TWRP GPS update.
set -eu
expected_gps=102b7ea8f6536baa6a7db7fc8a946f5d29f49e387519bc7d8580489a2f671df2
[ "$(getprop sys.boot_completed)" = 1 ]
[ "$(uname -r)" = "7.3.0-pjarczak" ]
[ "$(getenforce)" = Enforcing ]
[ "$(getprop init.svc.vendor.lt013g-gps-legacy-v195)" = running ]
[ "$(getprop init.svc.cpboot-daemon)" = running ]
[ "$(getprop init.svc.ril-daemon)" = running ]
[ "$(getprop init.svc.t311-audio-ril)" = running ]
actual_gps="$(sha256sum /vendor/lib/hw/gps.default.so | cut -d ' ' -f 1)"
[ "$actual_gps" = "$expected_gps" ]
usb="$(dumpsys usb)"
printf '%s\n' "$usb" | grep -Fq 'current_functions=MTP'
printf '%s\n' "$usb" | grep -Fq 'current_functions_applied=true'
printf '%s\n' "$usb" | grep -Fq 'configured=true'
echo G621_GPS_ONLY_PHYSICAL_POSTINSTALL_ACCEPTANCE_PASS
