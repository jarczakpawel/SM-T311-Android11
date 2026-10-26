G714 - read-only physical G650 CPU, ZRAM, memory and battery evidence
===================================================================

2026-10-21, Samsung Galaxy Tab 3 SM-T311, Android 11, Linux 7.3.0-pjarczak.

New Android-side instrumentation, kept in the device's Git repository:
device/samsung/lt013g/tools/g714-readonly-resource.sh. This source
was pushed ONLY to /data/local/tmp/g714-readonly-resource.sh, executed
with uid=2000 shell, and device SHA256 was verified exactly against
the Git source SHA256:
167125f7bd37840f63c03f880f54eb1186b3032db4909f041ba8d171cef69f2f.
It did not change the device's system, vendor, kernel, BOOT or RECOVERY
partitions, governor, SELinux, swap, ZRAM, modem, calls or audio.

Full sanitized shell output preserved as
Documentation/mm/sm-t311-g714-physical-readonly-sample.txt and in
project G714_PHYSICAL_READONLY_SAMPLE_20261021.txt. No identifying user
files, phone numbers, network addresses or application messages.

Host analyzer and regression gate::

  make -C tools/testing/selftests/smt311 check-passive-resource-g714

This runs tools/testing/selftests/smt311/g714_passive_resource_gate.py
using the archived sample and the actual G700 host .config and Exynos
DT OPP source. It DOES NOT query the device or perform CPU benchmarks.
Machine-readable result:
Documentation/mm/sm-t311-g714-physical-resource-report.json.

All 25/25 source/config/physical evidence predicates PASS; all 9/9
intentionally altered telemetry inputs are detected, including
unexpected swap pressure, OOM, direct reclaim, loss of available
memory, counter rollback, 1500MHz frequency, incomplete boot and
malformed residency rows.

Grounded physical resource observation
--------------------------------------

At uptime 48144.66..48148.73 seconds (4.07-second pair):

* Physical RAM MemTotal 1,483,332 KiB; MemAvailable 893,736 KiB.
* Active SWAP 786,428 KiB / 786,428 KiB free, matching the
  previously established 768 MiB ZRAM initialization. Physical
  ZRAM sysfs compressor/disksize/mm_stat cannot be read by UID2000
  shell; CONFIG_ZRAM_DEF_COMP="lz4" is the host-G700 configured
  default, NOT proof of the live compressor in this capture.
* /proc/vmstat cumulative pswpin 0, pswpout 0, oom_kill 0,
  pgscan_direct 0, pgsteal_direct 0, allocstall_normal/high/movable
  all 0, compact_stall 0. Thus no swap-out, direct reclaim, OOM
  or allocator stalls were registered as of the snapshot. This
  does NOT guarantee behavior under future RAM pressure.
* Physical CPU online 0-1, governor schedutil, min 200000 kHz,
  cpuinfo max 1400000 kHz. The Exynos4212 DTS lists the
  1.4GHz OPP; 1.5GHz OPP is marked turbo and never accrued
  time in either cpufreq residency sample.
* 200MHz time-in-state delta represented 61.43% of ~4 seconds,
  1400MHz 31.70%, other freq 6.87%. These are POLICY
  RESIDENCY fractions, NOT CPU idle/busy utilization. Both low
  and high frequency ticks progressed; a snapshot cur_freq at
  1.4GHz does NOT prove a stuck governor.
* Physical battery present 100%, USB charging, reported
  temperature 28.4 C. This is BATTERY temperature, not the
  SoC junction/thermal zone. No battery-drain, overnight standby,
  frequency-throttling or sustained-power test.
* HighTotal 670720 KiB / HighFree 1156 KiB; LowTotal
  812612 KiB / LowFree 296980 KiB. HighMem free can be low
  while reclaimable page cache is large, and MemAvailable high.
  Do not construe low HighFree alone as an OOM indication.
* /proc/stat rejects the unprivileged read with EINVAL in this
  environment, so CPU busy percentage was not measured.

Decision and deployment
-----------------------

No memory pressure or stable stuck-frequency problem was observed.
No justification exists yet to increase ZRAM, alter swappiness,
replace schedutil, unlock 1500MHz, or throttle the GPU/CPU. The
next meaningful measurement would be a supervised user workload
with known frame latency and battery drain if needed, or a
separate concrete malfunction. Avoid synthetic night audio/calls.

Only the selftest/host reporter, Android read-only collector and
archived sanitized snapshot were added; no runtime kernel/Android
executable system code was changed. Nothing was flashed.

S0 PASS (last known physical G650), S1 ACTIVE, S2-S6 BLOCKED,
S7 PARTIAL, S8 ACTIVE. S6 needs rooted physical p9/p10 SHA and
supervised reliable TWRP return before any G700 BOOT install.
