#!/system/bin/sh
# G719/G720: safe, read-only NMEA diagnostic, no location session started.
# Zero args: archived logcat sample (may be STALE, never call current).
# One numeric arg: include ONLY logs with epoch timestamp in last N sec.
# No root, location data, GNSS restart, ADB/network changes or backup.
set -eu
SCRIPT=/data/local/tmp/g719_gps_nmea_health.awk
test -r "$SCRIPT" || { echo "G719 AWK missing: $SCRIPT" >&2; exit 2; }
echo "G719_LIVE_HAL_SHA=$(sha256sum /vendor/lib/hw/gps.default.so | awk '{print $1}')"
echo "G719_GNSS_SERVICE=$(getprop init.svc.vendor.gnss_service)"
echo "G719_LEGACY_SERVICE=$(getprop init.svc.vendor.lt013g-gps-legacy-v195)"
if [ "$#" -eq 0 ]; then
  echo "LOGCAT_ARCHIVE_MAY_BE_STALE=1"
  logcat -d -t 16000 -v raw -s GpsOutputNmea:D '*:S' | awk -f "$SCRIPT"
elif [ "$#" -eq 1 ]; then
  case "$1" in *[!0-9]*|'') echo "G720 FAIL: seconds must be numeric" >&2; exit 2;; esac
  if [ "$1" -lt 1 ] || [ "$1" -gt 3600 ]; then
    echo "G720 FAIL: seconds outside 1..3600" >&2; exit 2
  fi
  CUTOFF=$(($(date +%s) - $1))
  echo "LOGCAT_RECENT_WINDOW_SECONDS=$1"
  # Numeric epoch columns let us reject historical sessions safely.
  logcat -d -t 16000 -v epoch -s GpsOutputNmea:D '*:S' |
    awk -v cutoff="$CUTOFF" '$1 ~ /^[0-9]+\.[0-9]+$/ && ($1 + 0) >= cutoff { print }' |
    awk -f "$SCRIPT"
else
  echo "Usage: sh $0 [recent_seconds_1_to_3600]" >&2
  exit 2
fi
echo "G719_END"
