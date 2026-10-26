# G414 / SM-T311: experimental per-UID CPU frequency accounting core

HOST RESEARCH ONLY; UNSHIPPABLE. This code is NOT compiled into Android,
Linux, BOOT, recovery, or the currently staged signed G408 release.
Do not install, flash, insmod or present as a working /proc/uid_time_in_state.

g414_event_core.c and g413_event_core.h model CPU nanosecond attribution
for two shared-policy cores. The latest change checks both CPU charges
before any shared-policy frequency change or UID removal, preventing partial
writes when a UID pool is exhausted or a u64 accumulator overflows.

Tests: native ASan/UBSan 75000 independent event oracle cases, deterministic
CPU transition/birth/UID purge cases, and 4095/4096 UID slot + combined u64
overflow atomic-reject tests. Cross-compiled ELF32 ARM32 object PASS.

Remaining: physical frequency transition lag relative to CPUFreq tracepoint,
real Linux scheduler/hotplug and CPU sleep, Android procfs 14-frequency ABI,
real UID remove_range/BOOTTIME purge, SELinux and actual ARM SMP/power trials.
Original physical G379 and signed G408 left intact.

## Updated full ARM32 host evidence 2026-10-17
The research-only built-in Linux6.12 Kconfig patch now passes clean isolated
full static ARM32 vmlinux link and compressed zImage build. Host zImage:
6319488 bytes, SHA256 fe74f3e0428a25ce6f092ecd8c9b508529cd310bb7572979e1a3de48dd8fe81a.
The actual artifact is outside this Android device repository in the private
Samsung engineering workspace. The local patch is default N / unshippable;
no /proc/uid_time_in_state, real G410 UID purge, or physical runtime acceptance.
Original signed G408 and working Android G379 remain unchanged.
The original future-only Linux Kconfig patch is stored as a byte-preserved
G414-FUTURE-LINUX612-BUILTIN-TWOCPU-ATOMIC-NOT-FLASHABLE.patch.gz because
embedded upstream source lines have whitespace which git diff --check treats
as new-file whitespace errors. Decompress in an isolated source clone ONLY.
