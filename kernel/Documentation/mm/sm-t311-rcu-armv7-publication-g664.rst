SM-T311 G664: ARMv7 tag publication, XArray and RCU-GP audit
===========================================================

Scope
-----
The tested board is dual Cortex-A9 with CONFIG_SMP=y and
CONFIG_PREEMPT_RCU=y. Kernel source files below are from the local
Linux 7.3 fork; the physical BOOT still contains the unchanged G650
runtime. This work intentionally changes no production kernel code.

Verified source chain
---------------------
In mm/filemap.c, filemap_mark_hot() runs with the ordinary read path
holding a folio reference. It checks an order-0 ZONE_NORMAL whitelist
and other attributes before calling folio_test_set_rcu_free(). The
folio flag helper expands from include/linux/page-flags.h to an atomic
test_and_set_bit() operation, not a plain assignment.

With CONFIG_SMP on ARMv7, arch/arm/include/asm/bitops.h sends
test_and_set_bit() to _test_and_set_bit(). arch/arm/lib/testsetbit.S
instantiates the testop macro from arch/arm/lib/bitops.h. The macro
has an smp_dmb barrier both BEFORE the ldrex/strex loop and AFTER its
successful store. arch/arm/include/asm/barrier.h maps the SMP barrier
to dmb(ish). This establishes the concrete instruction-family path
behind the flag setter; it is NOT an end-to-end ARM weak-memory proof.

The reader in filemap_read_rcu_lookup() tests the flag with READ_ONCE,
then smp_rmb, then i_size_read and a second ordering barrier before
xas_reload(). It checks order, zone and mapping after another barrier.
The 32-bit i_size seqcount is covered by earlier separate tests.
The read batch uses rcu_read_lock() plus pagefault_disable() and ends
the read critical section after the copy.

XArray provenance is subtle. lib/xarray.c xas_load() is permitted under
RCU; xa_node_free() calls call_rcu(). include/linux/xarray.h xa_entry()
uses rcu_dereference_check(), while xas_reload() accesses a cached
node's slot. It is NOT a generation-identity check. A cached node
protected from deallocation by RCU is not, from these source anchors
alone, proven to remain attached to the current XArray root.

mm/page_alloc.c checks the flag before returning memory to the buddy
allocator. If tagged, it clears the flag, queues deferred free and
returns false. The regular RCU work path uses queue_rcu_work(), and the
emergency flush path uses synchronize_rcu_expedited() before recycling.
The memory-failure code has two separate synchronize_rcu() exceptions,
and a large-folio split propagates __PG_RCU_FREE ahead of removing
the tail designation. These special paths still need whole-callgraph
and concurrency review.

Machine-checked source gates
----------------------------
tools/testing/selftests/smt311/rcu_armv7_publication_g664.py checks
23 source invariants across the ARM architecture, filemap, allocator,
XArray, memory-failure and split implementations. Its output is
Documentation/mm/sm-t311-rcu-armv7-publication-g664.json.

All 23 guards pass against current sources. Five negative mutations
actually rerun the full guard set on modified source text and must be
rejected: remove an ARM assembly bitop barrier, the atomic folio mark,
XArray node call_rcu, queue_rcu_work, or expedited synchronize_rcu.

Caveats and acceptance
----------------------
This is a fail-closed structural gate, not herd7, a C/ARMv7 interleaving
proof, actual disassembly of the physically flashed image, or closed-
world proof that no caller bypasses the deferral and grace mechanisms.
The G663 mixed-generation same-address scenario, concurrent truncation,
inode teardown, HighMem mapping/alias, DMA and all special free paths
remain safety obligations. Do not treat barriers alone as proof of
absence of UAF, stale data or corruption. No RCU no-ref ON, no BOOT
flash, and no promotion of S2 or S3.

Stages: S0 PASS, S1 ACTIVE, S2 BLOCKED, S3 BLOCKED, S4 BLOCKED,
S5 BLOCKED, S6 BLOCKED, S7 PARTIAL.
