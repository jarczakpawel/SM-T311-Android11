SM-T311 G674: cross-inode, three-generation ABA during no-ref reads
=====================================================================

Status
------
STAGED, NOT INSTALLED. This is a conservative source correction in the
experimental ARMv7 SM-T311 build configuration only. RCU no-ref remains
default OFF and is NOT approved for physical activation. Compilation
and structural/regression tests alone cannot close S2/S3.

Reader and writer event ordering
--------------------------------
The existing fastpath (mm/filemap.c) does:

  rcu_read_lock
  xas_load (pointer to folio at address P from address_space M)
  READ_ONCE(P->flags) & __PG_RCU_FREE
  smp_rmb and inode M size check
  xas_reload equals pointer P
  smp_rmb and folio order0/ZONE_NORMAL/mapping==M check
  _copy_to_iter(P data)
  rcu_read_unlock

In this ordering the flag load may refer to a DIFFERENT generation of
the physical page than the final xas_reload and mapping check.

The source-derived stress case has three objects occupying P:

  A: initially unmarked page-cache folio for inode M, at index I
  B: later marked/hot folio of inode N, using the recycled address P
  C: later unmarked folio for inode M, at the same index I/address P

After the reader loads pointer P from M's XArray entry for A, the
writer can remove/unreference/free A with no grace period. It may
then allocate B for unrelated inode N. B becomes hot and tagged
PG_rcu_free; the earlier reader for M can observe B's tag. Because
no reader holds inode N, inode N can go through final eviction and
its AS_EXITING exception previously explicitly cleared B's tag.
It could then release B without the usual deferred free, recycling
P again. C can be installed into M at the same index; the reader's
pointer-comparison xas_reload and folio->mapping==M both may match,
even though the tag observed earlier belonged only to B. C may be
removed and freed without a grace period before R copies it.

This is a strong, source-grounded check/use warning, NOT a confirmed
live UAF, memory corruption, guaranteed allocation order, or proof
that the real XArray under ARMv7 will allow every necessary stale read.
The model deliberately does not assume cache coherence, RCU epoch
subtleties, allocator PFN reuse likelihood, or other writer locks.
It is safer to eliminate one condition than enable a speculative
reader under unresolved safety.

Fix, scoped to the SM-T311 experiment
-------------------------------------
Previously page_cache_delete() and page_cache_delete_batch() had::

  if (folio_test_rcu_free(folio) && mapping_exiting(mapping))
          folio_clear_rcu_free(folio);

For CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT each exit-time early clear is
now guarded with::

  if (!IS_ENABLED(CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT) &&
      folio_test_rcu_free(folio) && mapping_exiting(mapping))
          folio_clear_rcu_free(folio);

The compiler eliminates the shortcut in the SM-T311 experiment so
*tagged folios remain tagged through final put*. __free_pages_prepare
then queues tagged pages for grace-protected physical freeing instead
of allowing B to be reused while a reader may still be using B's tag.
Non-SM-T311 builds preserve upstream behavior. Even when the feature
switch is turned OFF at runtime, tagged pages from earlier activity
are protected until freed. No new allocation metadata or hot-read lock
is introduced.

Costs and caveats
-----------------
An evicted inode can now leave more tagged folios waiting for a grace
period and increase RCU free queues; the existing Normal-zone pending
soft budget and free reserve are intended as guards but NOT proven
sufficient under extreme eviction/reclaim. Study starvation, OOM,
backlog growth, memory hotplug, hwpoison, migration, mapping lifetime
and cache alias coherency before any runtime activation. This patch
does NOT eliminate all generations, nor establish that XArray node
memory ordering or other tag-clearing paths are safe.

Verification
------------
Run::

  python3 tools/testing/selftests/smt311/rcu_cross_mapping_aba_g674.py

Result: 14/14 structural source conditions passed, seven altered source
variants rejected. The sequentially-consistent 19-event schedule
enumerator examined 75,582 order-preserving interleavings. Historical
upstream early-clear mode admitted 11 illustrative traces where C was
retired before copy, while the source model with retained B tag admitted
zero. The no-witness result is not a full correctness proof.

Independent regression scripts G660, G661, G666, G667 and G673 also
pass. The revised mm/filemap.c compiled as an ARM 32-bit EABI5 object
with the real SM-T311 defconfig and arm-linux-gnueabihf-gcc, in a
separate output directory. Its SHA256 was:
d6a24137d414297148a77ac3ec4dadd8cf708a3985edac224c1f6518d5b68c65.
This is one object, not a linked, signed, boot-tested zImage.

Production G650 remains flashed on the device; kernel HEAD after this
patch differs from its runtime code. DO NOT claim that the newly
staged G674 patch was physically installed, and do not flash just
because a host compilation passed. S0 PASS, S1 ACTIVE, S2 BLOCKED,
S3 BLOCKED, S4/S5/S6 BLOCKED, S7 PARTIAL, S8 ACTIVE.
