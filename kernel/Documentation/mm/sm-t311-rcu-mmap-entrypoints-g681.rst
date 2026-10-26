SM-T311 G681: mmap and RCU reader source audit
===============================================
This host-side source audit checks whether G680's synchronize_rcu
before publishing a new shared writable VMA under mmap_lock can be
bypassed by alternative publication paths.

Source owners of positive i_mmap_writable:

* mm/vma.c:mmap_region calls mapping_map_writable before VMA publish,
  with G680 waiting for an RCU grace period for previously-tagged
  file mappings.
* mm/mmap.c:dup_mmap increments the count when cloning an existing
  shared maywrite VMA; the parent VMA remains present.
* mm/vma.c:__vma_link_file increments the count for insertion or
  split of a pre-existing shared maywrite VMA.
* mm/mprotect.c changes VM_WRITE permissions but does not introduce
  a new shared VM_MAYWRITE mapping count.
* mm/mremap.c repositions extant VMAs without direct count changes.
* mm/memfd.c mapping_allow_writable is a rollback of a prior
  mapping_deny_writable call, not a first ext4 shared alias.

The specific ref-free reader uses pagefault_disable inside its RCU
critical section; it does not acquire mmap_lock. Hence the directly
audited reader is not a source-visible mmap_lock/RCU-GP wait cycle.
This does NOT close the global RCU/locking dependency graph. A GP
wait under mmap_lock can increase latency; S3 remains BLOCKED.

Results: 20/20 source predicates PASS; 10/10 mutated source controls
rejected by tools/testing/selftests/smt311/rcu_mmap_entrypoints_g681.py.
The output JSON records results without claiming hardware tests.
The code review does not establish all kernel lockdep paths, physical
ARMv7 cache correctness, memory pressure, or actual mmap latency.

G681 makes no executable kernel changes. The three already-staged
files mm/filemap.c, mm/page_alloc.c and mm/vma.c remain NOT INSTALLED.
Physical BOOT G650 and no-ref RCU default OFF are unchanged.
S0 PASS, S1 ACTIVE, S2-S6 BLOCKED, S7 PARTIAL, S8 ACTIVE.


G683 supersession: the G680 writer's mapping_rcu_read eligibility
gate was dropped in favor of an EXT4 source-type guard and mandatory
GP on new EXT4 shared writable mappings. G681's VMA origin
enumeration remains applicable, but the old predicate is not safe
against concurrent AS_RCU_READ/count publication. See G683.
