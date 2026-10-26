#!/bin/sh
# SM-T311 G765: disable idle Perfetto tracing only, leaving logd/statsd
# and every modem, GPS, camera, USB, media, OTA and Android service alone.
# On existing installations, persist.traced.enable is saved across restarts.
# Safe to run repeatedly from a normal host shell with working USB ADB.
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 USB_ADB_SERIAL" >&2
    exit 2
fi
serial="$1"
case "$serial" in
    ""|*:*|*.*|*[!A-Za-z0-9_-]*)
        echo "G765: expected local USB serial, not TCP endpoint" >&2
        exit 2 ;;
esac
adb -s "$serial" get-state | grep -qx device ||
    { echo "G765: USB ADB device unavailable" >&2; exit 2; }
[ "$(adb -s "$serial" shell getprop ro.product.device | tr -d '\r')" = "lt013g" ] ||
    { echo "G765: wrong device" >&2; exit 2; }
[ "$(adb -s "$serial" shell getprop sys.boot_completed | tr -d '\r')" = "1" ] ||
    { echo "G765: Android boot not complete" >&2; exit 2; }

adb -s "$serial" shell setprop persist.traced.enable 0
[ "$(adb -s "$serial" shell getprop persist.traced.enable | tr -d '\r')" = "0" ] ||
    { echo "G765: persistence property failed" >&2; exit 1; }
for service in traced traced_probes; do
    [ "$(adb -s "$serial" shell getprop "init.svc.$service" | tr -d '\r')" = "stopped" ] ||
        { echo "G765: $service remains running" >&2; exit 1; }
done
echo "G765_PASS: Perfetto daemons stopped; no reboot, no backup, no audio test"
