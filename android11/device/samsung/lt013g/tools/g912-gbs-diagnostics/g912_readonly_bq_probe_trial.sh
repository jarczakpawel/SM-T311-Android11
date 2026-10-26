#!/system/bin/sh
# G912 SM-T311: bounded one-shot native buffer-ownership diagnostic.
# No persistent writes, backups, BOOT/system changes, or broad services restart.
set -eu
mode="${1:---trial}"
old=250cebcf07e401b29266e1ca8270c5cdd279ca25dcb83c960359ab22779fadc0
new=cebdaca4fe2f7a9958b298515acdfb854a3c34d4f8660119df0a0bcf4102ada4
file=/system/lib/libstagefright_bufferqueue_helper.so
src=/data/local/tmp/G912_GBS_probed.so
flag=/data/local/tmp/G912_BIND_ACTIVE
sha() { sha256sum "$1" | cut -d' ' -f1; }
bound() { grep -F " $file " /proc/self/mountinfo >/dev/null 2>&1; }
restart_codec() {
 prev="$(pidof media.codec 2>/dev/null || true)"
 setprop ctl.restart vendor.media.omx
 n=0
 while [ "$n" -lt 14 ]; do
   sleep 1
   cur="$(pidof media.codec 2>/dev/null || true)"
   if [ -n "$cur" ] && [ "$prev" != "$cur" ] &&
      [ "$(getprop init.svc.vendor.media.omx)" = running ]; then
     echo "G912_MEDIA_CODEC_ALIVE_NEW_PID=$cur"
     return 0
   fi
   n=$((n+1))
 done
 echo G912_MEDIA_CODEC_RESTART_FAILED
 return 30
}
restore() {
 if bound; then umount -l "$file" || return 31; echo G912_GBS_DETACHED; fi
 [ "$(sha "$file")" = "$old" ] || return 32
 rm -f "$flag"
 restart_codec
 echo G912_ORIGINAL_SHA_AND_CODEC_RESTORED
}
case "$mode" in
 --restore) restore; exit $?;;
 --watchdog)
   sleep 65
   if [ -f "$flag" ] || bound; then restore; else echo G912_WATCHDOG_NO_ACTION; fi
   exit $?;;
 --trial) ;;
 *) echo G912_BAD_MODE;exit 2;;
esac
[ "$(id -u)" = 0 ] || exit 10
[ "$(getprop ro.product.device)" = lt013g ] || exit 11
[ "$(getprop ro.build.version.sdk)" = 30 ] || exit 12
[ "$(getprop sys.boot_completed)" = 1 ] || exit 13
[ "$(getenforce)" = Enforcing ] || exit 14
[ "$(getprop init.svc.vendor.media.omx)" = running ] || exit 15
[ "$(sha "$file")" = "$old" ] || exit 16
[ "$(sha "$src")" = "$new" ] || exit 17
[ "$(stat -c '%a' "$src")" = 644 ] || exit 18
ls -Z "$src" | grep -F 'u:object_r:system_lib_file:s0' >/dev/null || exit 19
[ ! -e "$flag" ] || exit 20
bound && exit 21
[ ! -e /data/local/tmp/G912_probe.mp4 ] || exit 22
setsid sh "$0" --watchdog >/data/local/tmp/G912_watchdog.log 2>&1 </dev/null &
touch "$flag"
trap 'restore' EXIT
mount -o bind "$src" "$file"
[ "$(sha "$file")" = "$new" ] || exit 23
restart_codec
echo G912_DIAGNOSTIC_ONLY_LIB_ACTIVE
start="$(date '+%m-%d %H:%M:%S')"
echo "G912_CAPTURE_START=$start"
am start -a android.settings.SETTINGS >/dev/null
screenrecord --size 640x1024 --bit-rate 6000000 --time-limit 12 \
  /data/local/tmp/G912_probe.mp4 >/data/local/tmp/G912_capture.log 2>&1 &
p=$!
sleep 2
input swipe 410 1030 410 280 950 >/dev/null
sleep 2
input swipe 410 1030 410 280 950 >/dev/null
sleep 2
input swipe 410 1030 410 280 950 >/dev/null
wait "$p"
echo G912_CAPTURE_COMPLETED_BEFORE_ROLLBACK
ls -l /data/local/tmp/G912_probe.mp4
logcat -b main,system -d -t 4800 | grep 'G912_GBS_REJECTED' | tail -n 12
input keyevent KEYCODE_HOME
exit 0
