SM-T311 G684: do not gate VMA grace by current runtime static key
=================================================================

Purpose
-------
Investigate reducing G683's cost of synchronize_rcu() under
mmap_lock when the experimental no-ref pagecache reader's
lockless_read_key is OFF. Specifically test the tempting change:

    if (lockless_read_enabled())
        synchronize_rcu();

in the writer mmap_region().

Finding
-------
That shortcut is not safe with the current switch protocol.

Writer W can observe OFF, then start to register its
VM_SHARED|VM_MAYWRITE ext4 VMA, publish i_mmap_writable,
and omit GP. A second thread can enable the ref-free read static
key and the reader can start, after which W can publish its VMA.
Merely calling synchronize_rcu() on the key-enabling thread does
NOT wait for completion of W's mmap publication. The toggle GP
is not a substitute for an independent GP that W omitted.

The lockless_read_set() debugfs function only performs memory-watermark
guard checks and static_branch_enable/disable. It does not coordinate
with another mm's mmap_lock or wait for in-flight mmap reservations.

The SM-T311 current build has CONFIG_PREEMPT_RCU=y and
CONFIG_JUMP_LABEL unset, so static_branch_likely is a test of the
static-key count rather than patching a per-site conditional jump.
That test is NOT itself a synchronization protocol ordering a
writer's VMA publication with a reader's first RCU critical section.

LKMM results
------------
Actual herd7 with tools/memory-model/linux-kernel.cfg and 3 actor
litmus models (writer, toggle thread, ref-free reader):

+-------------------------------------------+-----------+----------+----------+
| Protocol                                  | Outcome   | Positive | Negative |
+===========================================+===========+==========+==========+
| Writer skips GP if it sees runtime OFF    | Sometimes | 1        | 8        |
+-------------------------------------------+-----------+----------+----------+
| Each ext4 writer performs its own GP      | Never     | 0        | 8        |
+-------------------------------------------+-----------+----------+----------+
| OFF writer substitutes only smp_mb()      | Sometimes | 1        | 8        |
+-------------------------------------------+-----------+----------+----------+

This uses scalar key/count/published abstractions, not all Linux
VMA, mm, static-key or Cortex-A9 internals. An allowed observation
rules out an obvious shortcut in this model; it is NOT a physically
observed data-corruption or a full proof that the retained protocol
is safe.

Reproducible gate
-----------------
Run:

    python3 tools/testing/selftests/smt311/rcu_toggle_mmap_g684.py

It executes herd7 against the three checked-in litmus files, checks
18/18 source/build predicates, detects 8/8 deliberately modified
source controls, and verifies exact observation counts, SHA256
of input/output and the ARM32 objects built with experiment ON.

IMPORTANT build-correctness discovery:
--------------------------------------
The prior G674 staging directory
../build/g674-inode-aba-safety had CONFIG_DEBUG_FS NOT set, so Kconfig
disabled CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT and PAGE_RCU_FREE in
that build. Thus previously compiling mm/vma.o there did NOT exercise
the CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT guarded G680/G683 writer code.
The old successful ARM32 object builds must not be counted as proof
of correct compilation of that guarded code.

G684 repeated the compile of mm/vma.o, mm/filemap.o and
mm/page_alloc.o from the current kernel sources in
../build/g633-armv7-rcu-zone-guard, whose Kconfig actually has:

  CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y
  CONFIG_PAGE_RCU_FREE=y
  CONFIG_DEBUG_FS=y
  CONFIG_ARCH_EXYNOS4=y
  CONFIG_HIGHMEM=y
  CONFIG_SMP=y

All three resulting objects are ARM EABI5 relocatables. Their
SHA256 hashes (also in the G684 machine JSON):

  mm/vma.o       44b34ee4375bb401fe3084ec633c064dd968f63503ff43c05e8abfef9bf19870
  mm/filemap.o   3e9ec4fd60de0ac0917581c65c4064f37821fd0110d3ea800e95c48e71e46747
  mm/page_alloc.o 2c8cd516d5c81a843b783c7ae097af518c9034304a63d738d164ace6d8fa2873

arm-linux-gnueabihf-readelf -r mm/vma.o confirms a relocation
for synchronize_rcu. This proves the guarded code was compiled,
but these three objects are NOT a complete linked or boot-tested
kernel image.

Decision
--------
G684 introduces NO executable source change. Do NOT gate the
mmap writer GP on the runtime static key unless a new, verified
activation protocol also coordinates outstanding mmap writers
and their eventual VMA publication across distinct mm_structs.

The retained G683 per-new-ext4-shared-writable-mmap GP is a
candidate STAGED NOT INSTALLED. It incurs possible GP cost
even while runtime no-ref static key is OFF; global RCU/mmap_lock
lockdep, real Cortex-A9 latency, HighMem, PFN/ABA and Normal-zone
pressure are NOT proven safe.

Exactly THREE executable paths remain STAGED NOT INSTALLED vs
physical kernel G650 source a2fca4558: mm/filemap.c G674/G680,
mm/page_alloc.c G676, mm/vma.c G680/G683.

S0 PASS; S1 ACTIVE; S2 BLOCKED; S3 BLOCKED; S4 BLOCKED; S5 BLOCKED;
S6 BLOCKED; S7 PARTIAL; S8 ACTIVE.
