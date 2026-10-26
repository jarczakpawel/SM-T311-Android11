#!/system/bin/sh
# G1001: baseline+first-10s source-tagged per-process DMA-buf size histogram.
# Read-only; NO modification of driver, permissions, PIN/PUK, radio or kernel.
set -eu
video=/sdcard/Movies/G1000_CMA_PORTRAIT_PRIVATE.mp4
out=/data/local/tmp/G1001_DMABUF_SIZES_PRIVATE.txt
[ "$(getprop ro.product.device)" = lt013g ] || exit 10
[ "$(getprop sys.boot_completed)" = 1 ] || exit 11
[ "$(getenforce)" = Enforcing ] || exit 12
[ "$(sha256sum /system/vendor/lib/libv4l2_codec2_components.so | cut -d ' ' -f 1)" = 6137f22dad0e0d7670020d50fb86266c7755b24dd546cb7ce3830a2bb89f75f6 ] || exit 13
[ "$(sha256sum "$video" | cut -d ' ' -f 1)" = 4a29b7b8c0e916c03e7558f9075a21aab0016c7beb701e46773cea226c71404e ] || exit 14
[ ! -e /data/local/tmp/G1001_DIAG_ACTIVE ] || exit 15
touch /data/local/tmp/G1001_DIAG_ACTIVE
clean() {
  am force-stop com.android.gallery3d >/dev/null 2>&1 || true
  rm -f /data/local/tmp/G1001_DIAG_ACTIVE
}
trap clean EXIT
am force-stop com.android.gallery3d >/dev/null 2>&1 || true
sleep 2
snapshot() {
  phase="$1"
  echo "G1001_SAMPLE phase=$phase unix=$(date +%s)"
  grep -E '^CmaTotal:|^CmaFree:|^MemAvailable:' /proc/meminfo
  for proc in surfaceflinger com.android.gallery3d android.hardware.media.c2@1.0-service-v4l2 system_server; do
    for pid in $(pidof "$proc" 2>/dev/null || true); do
      [ -r /proc/"$pid"/comm ] || continue
      echo "G1001_PROCESS name=$proc pid=$pid"
      awk '
       FNR == 1 {if (seen && size > 0) {key=exporter ":" size; cnt[key]++; tot[key]+=size;} size=0; exporter="unknown"; seen=1}
       /^size:[ \t]/ {size=$2+0}
       /^exp_name:[ \t]/ {exporter=$2}
       END {
        if (seen && size > 0) {key=exporter ":" size; cnt[key]++; tot[key]+=size;}
        for (k in cnt) printf "G1001_BUF exp_size=%s n=%d total_bytes=%.0f\n",k,cnt[k],tot[k]
       }' /proc/"$pid"/fdinfo/* 2>/dev/null || true
    done
  done
}
{
  snapshot before
  am start -W -n com.android.gallery3d/.app.MovieActivity -a android.intent.action.VIEW -d "file://$video" -t video/mp4 --grant-read-uri-permission
  i=0
  while [ "$i" -lt 10 ]; do
    snapshot "play:$i"
    sleep 1
    i=$((i+1))
  done
  am force-stop com.android.gallery3d >/dev/null 2>&1 || true
  sleep 3
  snapshot after
  echo G1001_DONE
} >"$out" 2>&1
