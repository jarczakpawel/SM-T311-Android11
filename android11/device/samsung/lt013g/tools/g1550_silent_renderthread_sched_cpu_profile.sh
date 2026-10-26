#!/system/bin/sh
# G1550: CPU execution vs wall-time during identical 4 silent shade gesture cycles.
set -eu
[ "$(getprop ro.product.device)" = lt013g ] || exit 20
[ "$(getprop ro.build.version.incremental)" = SMT311G1441 ] || exit 21
[ "$(getprop sys.boot_completed)" = 1 ] || exit 22
dumpsys power | awk '/mWakefulness=Awake/ {yes=1} END {exit !yes}' || exit 23
dumpsys display | awk '/mGlobalDisplayState=ON/ {yes=1} END {exit !yes}' || exit 24
PID=$(pidof com.android.systemui)
[ -n "$PID" ] || exit 25
RT=
for path in /proc/"$PID"/task/*/comm; do
    read -r name < "$path" || :
    if [ "$name" = RenderThread ]; then
        RT=${path%/comm}
        RT=${RT##*/}
        break
    fi
done
[ -n "$RT" ] || exit 26
OUT=/data/local/tmp/G1550-SM-T311-RENDERTHREAD-CPU-WALL-FOUR-SWIPES.tsv
[ ! -e "$OUT" ] || exit 27
read_sched_field() {
    awk -v key="$2" '$1==key {print $3; found=1; exit} END {if (!found) exit 2}' "/proc/$PID/task/$1/sched"
}
get_uptime() { awk '{print $1}' /proc/uptime; }
printf 'gesture\twall_s_start\twall_s_end\trt_cpu_ms_start\trt_cpu_ms_end\trt_voluntary_start\trt_voluntary_end\trt_involuntary_start\trt_involuntary_end\tui_cpu_ms_start\tui_cpu_ms_end\n' > "$OUT"
input touchscreen swipe 400 5 400 1000 420
sleep 1
n=0
for i in 1 2 3 4; do
    for mode in open close; do
        n=$((n + 1))
        ws=$(get_uptime)
        cpu_before=$(read_sched_field "$RT" se.sum_exec_runtime)
        vol_before=$(read_sched_field "$RT" nr_voluntary_switches)
        invol_before=$(read_sched_field "$RT" nr_involuntary_switches)
        ui_before=$(read_sched_field "$PID" se.sum_exec_runtime)
        if [ "$mode" = open ]; then
            input touchscreen swipe 400 1200 400 150 420
        else
            input touchscreen swipe 400 5 400 1000 420
        fi
        we=$(get_uptime)
        cpu_after=$(read_sched_field "$RT" se.sum_exec_runtime)
        vol_after=$(read_sched_field "$RT" nr_voluntary_switches)
        invol_after=$(read_sched_field "$RT" nr_involuntary_switches)
        ui_after=$(read_sched_field "$PID" se.sum_exec_runtime)
        printf '%s-%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$n" "$mode" "$ws" "$we" "$cpu_before" "$cpu_after" "$vol_before" "$vol_after" "$invol_before" "$invol_after" "$ui_before" "$ui_after" >> "$OUT"
        sleep 1
    done
done
printf 'G1550_PHYSICAL_SCHED_SOURCE_READ_ONLY_CAPTURE_PASS\n'
printf 'G1550_PID=%s RT_TID=%s\n' "$PID" "$RT"
cat "$OUT"
