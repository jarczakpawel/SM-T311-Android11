#!/system/bin/sh
# G1154 SELinux read-only AVC sweep. Never auto-generate 'audit2allow' grants.
# No playback/phone calls/camera/mic, no root/setenforce, no files or backups.
# Reports only domain/type/class aggregate, NO private file paths or app IDs.
set -eu
[ "$(getprop ro.product.device)" = "lt013g" ] || {
  echo G1154_AVC_REFUSE_WRONG_DEVICE
  exit 7
}
[ "$(getenforce)" = Enforcing ] || {
  echo G1154_AVC_REFUSE_NOT_ENFORCING
  exit 8
}
echo G1154_AVC_SOURCE_LAST_10000_LOGCAT_LINES_NOT_FULL_USER_SESSION
logcat -b all -d -v brief -t 10000 2>/dev/null | awk '
/avc: denied/ {
  source="unknown"; target="unknown"; cls="unknown"
  for (i=1; i<=NF; i++) {
    if ($i ~ /^scontext=/) { source=$i; sub(/^scontext=/,"",source); }
    if ($i ~ /^tcontext=/) { target=$i; sub(/^tcontext=/,"",target); }
    if ($i ~ /^tclass=/) { cls=$i; sub(/^tclass=/,"",cls); }
  }
  # No filename, inode, path, package ID, actual private log line.
  key=source " -> " target " class=" cls
  occurrences[key]++
  denied++
}
END {
  if (denied==0) {
    print "PASS G1154_AVC_NO_DENIAL_IN_RECENT_BUFFER"
  } else {
    print "NEEDS_CAUSAL_REVIEW G1154_AVC_DENIAL_COUNT=" denied
    for (key in occurrences) {
      print "G1154_AVC_AGGREGATE count=" occurrences[key] " " key
    }
  }
  print "NOTE G1154_AVC_IDLE_ZERO_DOES_NOT_QUALIFY_UNEXERCISED_AUDIO_VIDEO_GPS_OTA"
}'
