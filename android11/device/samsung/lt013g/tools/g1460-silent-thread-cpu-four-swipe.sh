#!/system/bin/sh
set -eu
[ "$(getprop ro.product.device)" = lt013g ] || exit 20
[ "$(getprop ro.build.version.incremental)" = SMT311G1441 ] || exit 21
[ "$(getprop sys.boot_completed)" = 1 ] || exit 22
dumpsys power | awk '/mWakefulness=Awake/ {seen=1} END {exit !seen}' || exit 23
dumpsys display | awk '/mGlobalDisplayState=ON/ {seen=1} END {exit !seen}' || exit 24
ui="$(pidof com.android.systemui)"
sf="$(pidof surfaceflinger)"
[ -n "$ui" ] && [ -n "$sf" ] || exit 25
target=/data/local/tmp/G1460-CPU-THREADS-FOUR-SWIPES.txt
[ ! -e "$target" ] || exit 26
before=/data/local/tmp/G1460-CPU-THREADS-BEFORE.tmp
after=/data/local/tmp/G1460-CPU-THREADS-AFTER.tmp
capture() {
    for pair in "SystemUI:$ui" "SurfaceFlinger:$sf"; do
        name="${pair%%:*}"
        pid="${pair#*:}"
        for stat in /proc/"$pid"/task/*/stat; do
            [ -r "$stat" ] || continue
            tid="${stat%/stat}"
            tid="${tid##*/}"
            awk -v process="$name" -v tid="$tid" '
            {
              name=$0
              sub(/^[^(]*\(/, "", name)
              sub(/\) .*$/, "", name)
              rest=$0
              sub(/^.*\) /, "", rest)
              split(rest, f, " ")
              if (f[12] ~ /^[0-9]+$/ && f[13] ~ /^[0-9]+$/) {
                print process, tid, name, f[12]+f[13]
              }
            }' "$stat"
        done
    done
}
capture > "$before"
begin="$(awk '{print $1}' /proc/uptime)"
for i in 1 2 3 4; do
    input touchscreen swipe 400 1200 400 150 420
    sleep 1
    input touchscreen swipe 400 5 400 1000 420
    sleep 1
done
finish="$(awk '{print $1}' /proc/uptime)"
capture > "$after"
awk -v start="$begin" -v end="$finish" '
NR==FNR { key=$1 SUBSEP $2; ticks[key]=$4; labels[key]=$1 " " $2 " " $3; next }
{
 key=$1 SUBSEP $2
 if (key in ticks && $4 >= ticks[key]) {
   used=($4-ticks[key])*10
   if (used>0) print labels[key], used
 }
}
' "$before" "$after" | sort -k4,4nr > "$target"
printf 'G1460_TOTAL_WALL_MS='
awk -v start="$begin" -v end="$finish" 'BEGIN {printf "%.0f\n", (end-start)*1000}'
printf 'G1460_CPU_TIME_TOP_THREADS:'
head -n 25 "$target"
printf 'G1460_THREAD_RECORD_PATH=%s\n' "$target"
rm -f "$before" "$after"
printf 'G1460_NO_SOUND_NO_REBOOT_NO_FTRACE_NO_KERNEL_CHANGE_NO_BACKUP\n'
