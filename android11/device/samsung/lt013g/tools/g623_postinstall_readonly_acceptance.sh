#!/system/bin/sh
# G623 physical production acceptance: READ ONLY, no modem operations.
set -eu
file_hash() { sha256sum "$1" | cut -d ' ' -f1; }
check() { [ "$(file_hash "/vendor/bin/$1")" = "$2" ]; }
[ "$(getprop sys.boot_completed)" = 1 ]
[ "$(uname -r)" = 7.3.0-pjarczak ]
[ "$(getenforce)" = Enforcing ]
[ "$(getprop init.svc.cpboot-daemon)" = running ]
[ "$(getprop init.svc.ril-daemon)" = running ]
[ "$(getprop init.svc.t311-audio-ril)" = running ]
[ "$(getprop init.svc.vendor.lt013g-gps-legacy-v195)" = running ]
check t311-audio-ril afcfb90944f53746c05e3bd033d5421782a0428eccf473b8060fe133603f8e65
check t311-mobile-session-probe 8b7f15f0f58ef225c56a39dccec9dea232d6cb3937eaea818e56aafe058fd330
check t311-modem-watchdog f8b203ff22d101894e7ffa7c0c48cd4d7ca93c9b2bdf3a71c5926a35adfff86c
check t311-cbd-pidfd-signal e9b08ba4055d8ee3f988d80bc3bfb8df7b0af204ec4214102704c5f5178ef048
[ "$(file_hash /vendor/lib/hw/gps.default.so)" = 102b7ea8f6536baa6a7db7fc8a946f5d29f49e387519bc7d8580489a2f671df2 ]
usb="$(dumpsys usb)"
printf '%s\n' "$usb" | grep -Fq current_functions=MTP
printf '%s\n' "$usb" | grep -Fq current_functions_applied=true
printf '%s\n' "$usb" | grep -Fq configured=true
echo G623_FOURFILE_PHYSICAL_POSTBOOT_ACCEPTANCE_PASS
