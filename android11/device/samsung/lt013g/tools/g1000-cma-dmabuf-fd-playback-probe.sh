#!/system/bin/sh
# G1000: read-only CMA and dma_buf FD pressure proof during REAL HW portrait decode.
# No mounts, SIM queries, reboots, code install, backups, debugfs changes, or log wipes.
# Only opens already private, SHA-pinned regression clip and stops its Gallery player.
set -eu
video=/sdcard/Movies/G1000_CMA_PORTRAIT_PRIVATE.mp4
out=/data/local/tmp/G1000_CMA_FD_TRACE_PRIVATE.txt
log=/data/local/tmp/G1000_PLAYER_ERRORS_PRIVATE.txt
[ "$(getprop ro.product.device)" = lt013g ] || exit 10
[ "$(getprop sys.boot_completed)" = 1 ] || exit 11
[ "$(getenforce)" = Enforcing ] || exit 12
[ "$(sha256sum /system/vendor/lib/libv4l2_codec2_components.so | cut -d ' ' -f 1)" = 6137f22dad0e0d7670020d50fb86266c7755b24dd546cb7ce3830a2bb89f75f6 ] || exit 13
[ "$(sha256sum "$video" | cut -d ' ' -f 1)" = 4a29b7b8c0e916c03e7558f9075a21aab0016c7beb701e46773cea226c71404e ] || exit 14
[ ! -e /data/local/tmp/G1000_DIAG_ACTIVE ] || exit 15
touch /data/local/tmp/G1000_DIAG_ACTIVE
finish() {
  am force-stop com.android.gallery3d >/dev/null 2>&1 || true
  rm -f /data/local/tmp/G1000_DIAG_ACTIVE
}
trap finish EXIT
am force-stop com.android.gallery3d >/dev/null 2>&1 || true
sleep 2
start="$(date +%s)"
{
 echo "G1000_BEGIN unix=$start"
 i=0
 while [ "$i" -lt 29 ]; do
  echo "SAMPLE idx=$i unix=$(date +%s)"
  grep -E '^CmaTotal:|^CmaFree:|^MemAvailable:' /proc/meminfo
  for name in surfaceflinger system_server com.android.gallery3d android.hardware.media.c2@1.0-service-v4l2 mediaserver; do
    p="$(pidof "$name" 2>/dev/null || true)"
    [ -n "$p" ] || continue
    for pid in $p; do
      [ -r "/proc/$pid/comm" ] || continue
      printf 'FD owner=%s pid=%s ' "$name" "$pid"
      awk '/^size:/ { n++; b+=$2; if ($2>mx)mx=$2 }
           END { printf "count=%d sum_fd_bytes=%.0f max_fd_bytes=%.0f\n", n,b,mx }' /proc/"$pid"/fdinfo/* 2>/dev/null || true
    done
  done
  if [ "$i" -eq 1 ]; then
    echo "G1000_START_PLAYER"
    am start -W -n com.android.gallery3d/.app.MovieActivity -a android.intent.action.VIEW -d "file://$video" -t video/mp4 --grant-read-uri-permission || true
  fi
  sleep 1
  i=$((i+1))
 done
 echo G1000_STOP_PLAYER
 am force-stop com.android.gallery3d >/dev/null 2>&1 || true
 sleep 3
 echo G1000_FINAL_CMA
 grep -E '^CmaTotal:|^CmaFree:|^MemAvailable:' /proc/meminfo
 echo G1000_END
} > "$out" 2>&1
logcat -b main -d -v threadtime | grep -E 'G998_PORTRAIT_CMA_TRIAL|Q29E_MFC_CAPTURE_TM12|previous call to queue exceeded timeout|Decoder \(video\) reported error|Codec reported err|C2VdaBqBlockPool.*failed|Q29L_DRAIN_COMPLETE' | tail -140 > "$log" || true
echo G1000_COMPLETED_TRACE_AND_LOG
