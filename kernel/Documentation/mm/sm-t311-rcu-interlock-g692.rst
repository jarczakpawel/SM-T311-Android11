SM-T311 G692 — fail-closed debugfs switch for unproven ref-free RCU
===================================================================

Result
------
CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT continues to build the optional
ARMv7 page-cache reader and allocator code as an experimental host
test profile. However, enabling it through the debugfs file
"lockless_page_cache_read" is now FAIL-CLOSED:

* Write "1" (enable) returns -EOPNOTSUPP (-95).
* Write "0" (disable) remains supported. The static key is initially
  false (OFF) and the code cannot turn it ON through this setter.
* Writes above 1 continue to return -EINVAL (-22).
* Non-SM-T311 and default RCU OFF builds preserve their previous
  source path unchanged.

This is **not** a runtime experiment on the physical SM-T311; the
change is STAGED in local kernel Git. It prevents accidentally
activating a known-unproven optional no-reference read path if this
experimental build were ever packaged and installed in future.

Why it is warranted
-------------------
Prior G659 real herd7 LKMM synthetic models (four "Sometimes"
observations) and the G691 source-coupled allocator model showed
that the existing late PG_rcu_free -> xas_reload -> mapping pointer
checks do not establish an independent folio generation identity.
A reader can conceptually sample an initially UNTAGGED old folio's
raw pointer and observe an entirely new TAGGED occupant at the same
PFN and same inode/index after normal buddy reuse. The new
generation's GP cannot retroactively protect a raw pointer to the
untagged old generation.

G692 inspected the **existing working reference-counted path**
mm/filemap.c:filemap_get_entry(), whose in-tree documentation
explicitly describes:

  rcu_read_lock()
  xas_load() -> folio_try_get() -> xas_reload()
  if changed: folio_put() and retry
  rcu_read_unlock()

A successful reference plus slot revalidation protects the folio
during subsequent use. It is NOT equivalent to the optional
reference-free code. include/linux/xarray.h:xas_reload() returns
the current entry, not a generation counter.

The only legitimate eventual RCU-ON route needs a complete,
reviewed lifetime/generation mechanism together with ARMv7
cache/HighMem, memory-pressure and lockdep evidence. A source
comment or extra smp_rmb() does not solve the ABA ambiguity.

Implementation
--------------
mm/filemap.c:lockless_read_set(), under
CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT and following the original
invalid-value guard, now contains:

  if (val)
      return -EOPNOTSUPP;

Thus the compiled setter rejects ON and cannot reach the historical
activation checks, which remain in the staged source for
future analysis of Normal-zone reserve/DT compatibility. A
compiler-optimized but source-present enable path must NOT be
reported as genuinely available. Later work should reorganize
the activation logic only after S2/S3 sign-off.

The normal release-OFF build has neither the experimental reader
key nor experimental setter. No physical device was changed.

Tests
-----
Run both ARM32 host builds, then the actual source/disassembly gate:

  bash tools/testing/selftests/smt311/build_verify_no_flash_g686.sh
  bash tools/testing/selftests/smt311/build_verify_release_off_g687.sh
  python3 tools/testing/selftests/smt311/rcu_activation_interlock_g692.py

The G692 validator checks 18 C/Kconfig/XArray/reference-protocol
source anchors; rejects 12 genuine changed-source mutants.
It then inspects **real linked ARM32 EABI5 disassembly**:

- lockless_read_set accepts (void *, u64) with u64 val in r2/r3;
- nonzero value branches to an ARM MVN r0,#94; BX LR, i.e. -95;
- greater than 1 branches to the original -EINVAL;
- the zero/disable path is not directed to -EOPNOTSUPP;
- the normal release-OFF vmlinux does NOT link a setter;
- the experiment vmlinux still retains the debugfs setter.

7/7 actual assembly assertions and 2/2 counterfactual modified
assembly controls PASS. The source/ELF fingerprints are stored in
Documentation/mm/sm-t311-rcu-interlock-g692.json.

Historical G684/G685 models explored unsafe hypothetical toggle
protocols; their synthetic outcomes remain valuable but do not
represent an activation allowed by the new G692 host setter.

Limits and deployment
---------------------
No physical debugfs write was attempted. The gate is static-analysis
of cross-compiled code, not a test of full device behavior or a
mathematical lifetime proof. Other potential key manipulation paths
must be reviewed before declaring a globally unactivatable system.
The source search finds the debugfs setter as the only ordinary
static_branch_enable(&lockless_read_key) site, but arbitrary kernel
memory corruption/root modification is out of scope.

This does not demonstrate acceptable performance, PFN generation,
VIPT/HighMem cache coherence, safe global mmap_lock ordering or
Normal-zone pressure. S2 and S3 remain BLOCKED; actual RCU ON,
ABBA benchmarks and final production selection remain blocked.

Exactly FOUR modified kernel executable source files remain STAGED
NOT INSTALLED versus physical G650: mm/filemap.c, mm/page_alloc.c,
mm/shmem.c, mm/vma.c. The optional G686 Kconfig fragment is
a tracked host-test configuration file, not deployed runtime code.

Physical G650 Linux 7.3.0-pjarczak still runs on SM-T311. No
BOOT/TWRP flash, no backup, no reboot or nightly sound/camera/
modem/PIN/OTG tests. GitHub push is owner-only.

S0 PASS; S1 ACTIVE; S2 BLOCKED; S3 BLOCKED; S4 BLOCKED;
S5 BLOCKED; S6 BLOCKED; S7 PARTIAL; S8 ACTIVE.


Deterministic G692 host images
------------------------------
G692 also pins build identity in BOTH tracked host-only build scripts:

  KBUILD_BUILD_TIMESTAMP=2026-10-21T00:00:00Z
  KBUILD_BUILD_VERSION=1
  KBUILD_BUILD_USER=smt311
  KBUILD_BUILD_HOST=reproducible

The full linked ARMv7 outputs were generated TWICE from the same
tracked sources, compiler, build roots and configs. Both passes had
byte-identical SHA256 of each vmlinux and zImage:

* Experimental build with ON refused:
  vmlinux 6674571cbe202e69692a9b9279e6eb2b629b108d390719fb8296fd2e79f44ed4
  zImage 5d72282b95b43ef9309a0d4b1eaf4077f15118daece95882f784f1d0d36a82dd
* Normal release OFF:
  vmlinux e929387d60fec5233cab1c36ccdb6ad50e2d7699ccc22c7d231f9992e8cc97c8
  zImage 1c2dd2e521cbfec48c95c89518487b5cfd1887027e7f9d74146027d777e86c49

The in-kernel UTS_VERSION is now fixed to:
  #1 SMP PREEMPT 2026-10-21T00:00:00Z
Both still report kernelrelease 7.3.0-pjarczak.

The G692 test also checks 4/4 build-provenance assertions and rejects
2/2 deliberate source alterations to pinned build-script parameters.
Byte-identical output on this host does NOT prove reproduction across
different compilers, paths or distributions. No generated zImage has
been packaged as a Samsung BOOT or physically installed.
