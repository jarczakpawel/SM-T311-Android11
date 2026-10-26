#!/system/bin/sh
TAG=SMT311-PERF

write_if() {
    [ -w "$1" ] || return 0
    echo "$2" > "$1" 2>/dev/null || true
}

write_if /sys/devices/system/cpu/cpufreq/policy0/scaling_governor schedutil
write_if /sys/devices/system/cpu/cpufreq/policy0/schedutil/up_rate_limit_us 10000
write_if /sys/devices/system/cpu/cpufreq/policy0/schedutil/down_rate_limit_us 50000
write_if /sys/class/devfreq/13000000.gpu/governor simple_ondemand
write_if /sys/class/devfreq/bus-dmc/governor simple_ondemand
write_if /sys/block/mmcblk0/queue/scheduler bfq
write_if /sys/block/mmcblk0/queue/read_ahead_kb 256
write_if /sys/block/mmcblk0/queue/nr_requests 128
write_if /dev/blkio/background/blkio.weight 50
write_if /dev/blkio/background/blkio.bfq.weight 50
write_if /proc/sys/vm/swappiness 100
write_if /proc/sys/vm/page-cluster 0

log -t "$TAG" "stable userspace tuning applied" 2>/dev/null || true
exit 0
