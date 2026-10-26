# Samsung SM-T311 / Exynos4212 ARMv7-only reproducible selftests

These **userspace** programs are tested on Android 11 running Linux
7.3.0-pjarczak on Samsung Tab3 LTE SM-T311 (lt013g). They deliberately do
not modify kernel state, change /proc/sys, activate experimental refcount-free
RCU, reset SELinux, change SIM PIN, or touch user files. Only their private
files in /data/local/tmp are created and then cleaned on success.

Built from kernel Git itself, so the owner's eventual GitHub publication
includes actual runnable reproductions, not only experimental reports.

From this directory on a Debian amd64 host with arm-linux-gnueabihf-gcc:

    make -j4
    file *.arm32

Use adb push to stage a selected binary in /data/local/tmp and chmod 755.
Run only while the tablet is stable, with no active user work.

- **memfd_hotread.arm32**: private memfd 20k hot reads + truncate,
  extend and zero-fill integrity.
- **ext4_rename_truncate_race.arm32**: own ext4 directory; versioned
  rename versus two readers; additionally same-inode truncate/rewrite
  versus concurrent pread. *During a concurrent truncate the returned
  data may legitimately be partial/mixed*. Only syscall bounds and
  final quiescent content are checked.
- **pread_hotpage.arm32**: own single cached 4KiB ext4 file; 1/2
  threads pinned to Cortex-A9 CPU0/1. Usage THREADS(1|2)
  BYTES(16|4096) SECONDS(1..10); this is a hot-page microbenchmark.
- **pread_cacheline_isolation.arm32**: separate CPU0/CPU1 threads
  reading 16B, four modes shared, samefile, twopages, split.
  Usage MODE SECONDS(1..5). twopages uses SAME inode but distinct
  4KiB file offsets. Different page-cache indices are not an
  independent physical-folio-order proof.

For architecture analysis, record the *actual* ARMv7 PMU events:

    simpleperf stat -e cpu-cycles,instructions,cache-misses,branch-misses /data/local/tmp/pread_cacheline_isolation.arm32 twopages 2

Repeat in rotated mode order and compare medians, never only best runs.
Some Android simpleperf builds emit a non-fatal trace file startup error
while still returning the hardware counts: keep raw output and limitations.

**CRITICAL:** These programs use only ordinary reference-counted pagecache
paths with experimental refcountless RCU kept OFF. Passing these programs
does NOT prove refcount-free read safety, XArray ABA correctness, page
migration, HighMem cache aliases or benefits to real Android frame latency.
Never turn the experimental feature ON on an unattended tablet.

G651 on-device raw data, parser and high-level evidence are retained in
the project's Linux 7.3 migration ledger:
G651_SM_T311_PMU_FOLIO_VS_ANDROID_UI_20261020.md.
Results on G650 BOOT were PMU cache-misses ~4.5M for two readers of the
same 4KiB file region versus ~139k for two 4KiB regions of the same inode;
the read throughput difference was only ~0.24%, not 30x. The user owns
GitHub publication; do not auto-push.

No kernel configuration, module, driver, ramdisk or BOOT is changed by this
selftests directory.
