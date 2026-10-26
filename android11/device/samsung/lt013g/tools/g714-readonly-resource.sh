#!/system/bin/sh
# G714 SM-T311 silent resource diagnostics.
# READ ONLY: no su, remount, setprop, writes to sysfs, restarts, flashing,
# phone/ringtone/camera/GNSS activation or backups.
echo G714_RESOURCE_BEGIN
echo "KERNEL=$(uname -r)"
echo "BOOT_COMPLETED=$(getprop sys.boot_completed)"
echo "ADB_UID=$(id -u)"
echo "CPU_ONLINE=$(cat /sys/devices/system/cpu/online 2>/dev/null)"
p=/sys/devices/system/cpu/cpufreq/policy0
for pair in scaling_governor scaling_cur_freq scaling_min_freq cpuinfo_max_freq; do
  x=$(cat "$p/$pair" 2>/dev/null) || x=UNAVAILABLE
  echo "CPU_${pair}=$x"
done
for f in /sys/block/zram0/disksize /sys/block/zram0/comp_algorithm /sys/block/zram0/mm_stat; do
  if [ -r "$f" ]; then
    v=$(cat "$f" 2>/dev/null) || v=UNAVAILABLE
    [ -z "$v" ] && v=UNAVAILABLE
  else
    v=UNAVAILABLE
  fi
  echo "ZRAM_${f##*/}=$v"
done
echo MEMORY_BEGIN
grep -E '^(MemTotal|MemFree|MemAvailable|SwapTotal|SwapFree|HighTotal|HighFree|LowTotal|LowFree|Cached|AnonPages):' /proc/meminfo
echo MEMORY_END
echo VMSTAT_BEGIN
grep -E '^(pswpin|pswpout|oom_kill|pgscan_direct|pgsteal_direct|allocstall_normal|allocstall_high|allocstall_movable|pgmajfault|compact_stall) ' /proc/vmstat
echo VMSTAT_END
echo BATTERY_BEGIN
dumpsys battery | grep -E '^  (USB powered|status|health|present|level|temperature):'
echo BATTERY_END
echo START_UPTIME=$(cut -d' ' -f1 /proc/uptime)
echo CPU_TIS_START
cat "$p/stats/time_in_state" 2>/dev/null || echo UNAVAILABLE
echo CPU_TIS_START_END
sleep 4
echo END_UPTIME=$(cut -d' ' -f1 /proc/uptime)
echo CPU_TIS_END
cat "$p/stats/time_in_state" 2>/dev/null || echo UNAVAILABLE
echo CPU_TIS_END_END
echo G714_RESOURCE_END
