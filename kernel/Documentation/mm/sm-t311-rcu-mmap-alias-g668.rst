SM-T311 G668: writable mmap counter precheck versus RCU copy
==============================================================

Scope
-----
This is an actual Linux 7.3 source-path audit plus a bounded
sequentially-consistent event interleaving model. The result identifies
a temporal check/use interval, not a confirmed data corruption, UAF or
reproducible ARMv7 cache coherency defect. Physical no-ref RCU stays OFF.

Source paths
------------
include/linux/fs.h defines mapping_writably_mapped(mapping) as
atomic_read(&mapping->i_mmap_writable) > 0. Its documentation states
that the counter includes all VM_SHARED VM_MAYWRITE mappings, not
merely writable PTEs. This is an instantaneous observation and does
not hold i_mmap_rwsem or prevent another thread incrementing it.

mm/vma.c mmap_region() increments this counter via
mapping_map_writable() BEFORE calling __mmap_region(), while
__vma_link_file() keeps the shared-maywrite VMA accounted via
mapping_allow_writable(). The temporary increment is dropped after
the mmap operation. Thus a new eligible mapping causes a counter
increment before becoming accessible, but a reader checking zero
BEFORE that increment does not retain a counter or mapping lock.

mm/filemap.c filemap_read_rcu_batch() tests
mapping_writably_mapped(mapping) once, before rcu_read_lock() and
pagefault_disable(). It does not take i_mmap_rwsem or pin
i_mmap_writable. It then loads an eligible folio and directly copies
through filemap_read_rcu_copy() using _copy_to_iter(). RCU protects
specific object lifetimes only when the tagged last-free protocol
holds; it does not lock the mapping counter.

On this board, the defconfig enables CONFIG_CPU_CACHE_VIPT=y and
CONFIG_SMP=y. arch/arm/mm/flush.c has architecture-dependent cache
maintenance, including alias-sensitive branches and lazy-flush
handling. The actual Cortex-A9 cache geometry/aliasing behavior for
every VMA transition is not established by this source audit. It
would be inaccurate to equate a counter race with proved user data
corruption.

Reproducible bounded witness
----------------------------
tools/testing/selftests/smt311/rcu_mmap_alias_g668.py checks 16
specific source anchors (all PASS) and reruns those checks against
five separately mutated source inputs (all mutations REJECTED).

It also exhaustively enumerates 70 SC merges of the reader actions

  R_check_zero -> R_lock -> R_copy -> R_unlock

and the writer actions

  W_increment_count -> W_link_vma -> W_fault_page -> W_write_shared

preserving each thread's local order. Of 70 schedules, 35 check the
counter while zero; 22 of those admit a newly linked VMA before copy;
five admit a new shared write before copy. One witness:

  R_check_zero
  W_increment_count
  W_link_vma
  W_fault_page
  W_write_shared
  R_lock
  R_copy
  R_unlock

There is no ordering between this counter check and its later use
that forbids this history, even under sequential consistency.
This witness is about missing exclusion; it does NOT simulate a
real file cache, virtual alias, dirty line, C read/write race,
XArray or ARMv7 memory model.

Gates and decision
------------------
The runtime under G650 has the ref-free path disabled by default.
Do NOT enable it based on structural source checks or the SC model.
A possible fix would require end-to-end synchronization/proof that
does not break mmap latency, writer fault behavior or the intended
performance improvement. Simply rereading the counter immediately
before _copy_to_iter() is not a complete exclusion mechanism: the
writer can increment immediately after the second check.

The next obligations are actual shared writable-mmap fault entry,
ARMv7 cache alias coherence, i_mmap_rwsem/RCU read ordering, and
practical cost of any correct guard. The earlier G665 userspace
XArray binary remains UNRUN due missing liburcu development headers.

Script output stored in
Documentation/mm/sm-t311-rcu-mmap-alias-g668.json.

S0 PASS; S1 ACTIVE; S2 BLOCKED; S3 BLOCKED; S4 BLOCKED;
S5 BLOCKED; S6 BLOCKED; S7 PARTIAL. No BOOT flash, backup or
production source changes.
