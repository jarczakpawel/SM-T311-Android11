SM-T311 G688: remove empty no-ref RCU call from shmem release OFF
====================================================================

G687 observed that the 32-bit default SM-T311 release includes
a filemap_read_rcu symbol whose *entire implementation* under
CONFIG_PAGE_RCU_FREE=n is ARM 'mov r0,#0; bx lr'.

G687 removed a redundant call to this zero-return function in
filemap_read() when the experimental RCU feature is compiled OUT,
preserving the live RCU-capable route in experiment builds.

G688 independently located a second wasted call in
mm/shmem.c:shmem_file_read_iter(), executed by normal tmpfs/memfd
read requests. The existing experimental-SM-T311 route had already
set retval=0 because this experimental fast reader is restricted
to ext4, but the production RCU-OFF branch still called the stub.

Code change
-----------
Under CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT, retain retval=0.
Under non-SM-T311 but CONFIG_PAGE_RCU_FREE=y, preserve the
upstream-capable call to filemap_read_rcu().
When both options are disabled, set retval=0 without a call.

This modifies just mm/shmem.c. It does not change shmem folio
allocation, locking, i_size, error handling or the buffered
copy/fallback. No new RCU grace periods, rwsems, or memory-model
assumptions are introduced.

Reproduce with real fully linked ARM32 objects
----------------------------------------------
Build normal release OFF and separately the experiment-enabled
comparison from the Git-tracked tablet defconfig:

  bash tools/testing/selftests/smt311/build_verify_release_off_g687.sh
  bash tools/testing/selftests/smt311/build_verify_no_flash_g686.sh
  python3 tools/testing/selftests/smt311/rcu_shmem_stub_g688.py

G688 checks the before-code in Git G687 to prove the redundant
shmem call really existed, then uses actual source analysis and
ARMv7 objdump to verify:

* shmem_file_read_iter() release does not call filemap_read_rcu;
* shmem_file_read_iter() experiment does not call it either;
* filemap_read() release remains free of the stub call (G687);
* filemap_read() experiment still calls the real ref-free reader;
* release filemap_read_rcu remains a two-instruction zero stub;
* the release binary contains no lockless_read_key;
* the normal release version remains 7.3.0-pjarczak.

Source gates 8/8 PASS, changed-C mutant controls 6/6 rejected.
Actual ARM32 objdump conditions 6/6 PASS, altered-disassembly
negative tests 2/2 rejected. The script also checks timestamps,
SHA256 of both real zImages, both vmlinux and both shmem.o files,
and writes sm-t311-rcu-shmem-stub-g688.json.

Performance scope
-----------------
The number of redundant ARM function calls is reduced, but there
is no measured wall-clock, temperature or app-level improvement
from the physical dual Cortex-A9 device. Do not invent one.

The experimental G683 synchronize_rcu under mmap_lock (even key
OFF) remains; no source change here proves PFN/ABA, RCU lifetime,
HighMem/VIPT or global lock ordering safe.

G688 increases the explicit changed-source list versus the
physically booted G650 kernel source a2fca4558 from THREE to
FOUR runtime files:

- mm/filemap.c    G674/G680/G685/G687
- mm/page_alloc.c G676
- mm/shmem.c      G688
- mm/vma.c        G680/G683

The tracked arch/arm/configs/sm_t311_rcu_experiment_g686.fragment
is test-build configuration, not an executable runtime code
change and must continue to be accounted separately.

All FOUR are STAGED NOT INSTALLED. The physical G650 Android/BOOT
has not been modified. No BOOT repack, TWRP flash, backup,
audio/camera/phone or modem/PIN/OTG tests were run.

S0 PASS, S1 ACTIVE, S2 BLOCKED, S3 BLOCKED,
S4 BLOCKED, S5 BLOCKED, S6 BLOCKED,
S7 PARTIAL, S8 ACTIVE.
