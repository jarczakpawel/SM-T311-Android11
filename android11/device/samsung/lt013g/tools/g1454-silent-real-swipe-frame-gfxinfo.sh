#!/system/bin/sh
set -eu
[ "$(getprop ro.product.device)" = lt013g ] || exit 20
[ "$(getprop ro.build.version.incremental)" = SMT311G1441 ] || exit 21
[ "$(getprop sys.boot_completed)" = 1 ] || exit 22
dumpsys power | awk '/mWakefulness=Awake/ {found=1} END {exit !found}' || exit 23
dumpsys display | awk '/mGlobalDisplayState=ON/ {found=1} END {exit !found}' || exit 24
OUT=/data/local/tmp/G1454-SYSTEMUI-SHADE-FOUR-SWIPE-STATS.txt
[ ! -e "$OUT" ] || exit 25
input touchscreen swipe 400 5 400 1000 420
sleep 1
dumpsys gfxinfo com.android.systemui reset >/dev/null
for i in 1 2 3 4; do
    input touchscreen swipe 400 1200 400 150 420
    sleep 1
    input touchscreen swipe 400 5 400 1000 420
    sleep 1
done
dumpsys gfxinfo com.android.systemui > "$OUT"
awk '
    /^Total frames rendered:/ && !f++ { print "G1454_" $0 }
    /^Janky frames:/ && !j++ { print "G1454_" $0 }
    /^50th percentile:/ && !p++ { print "G1454_" $0 }
    /^90th percentile:/ && !p90++ { print "G1454_" $0 }
    /^Number High input latency:/ && !hi++ { print "G1454_" $0 }
    /^Number Slow UI thread:/ && !ui++ { print "G1454_" $0 }
    /^Number Slow issue draw commands:/ && !draw++ { print "G1454_" $0 }
    /^Number Slow bitmap uploads:/ && !bitmap++ { print "G1454_" $0 }
    /^50th gpu percentile:/ && !gp++ { print "G1454_" $0 }
    /^90th gpu percentile:/ && !gp90++ { print "G1454_" $0 }
' "$OUT"
dumpsys window | awk '/mCurrentFocus=/ && !done++ { print "G1454_" $0 }'
printf '%s\n' 'G1454_READONLY_PHYSICAL_GESTURE_STATS_NO_SOUND_NO_WAKE_NO_BACKUP_NO_FLASH'
