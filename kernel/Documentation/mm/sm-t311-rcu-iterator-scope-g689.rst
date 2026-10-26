SM-T311 G689 — restrict experimental reference-free file reads to user iterators
================================================================================

Motivation
----------
The experimental no-reference filemap_read_rcu() previously admitted
every destination iov_iter that reached filemap_read(), subject to
the ext4 inode, AS_RCU_READ, runtime key and Normal-zone constraints.

The source folio is mapped under RCU and copied using _copy_to_iter().
lib/iov_iter.c passes this to iterate_and_advance(). The downstream
destination classes have materially different execution graphs:

* ITER_UBUF and ITER_IOVEC: copy into a user buffer (may fault; existing
  pagefault_disable() and short-copy fallback apply).
* ITER_KVEC and ITER_BVEC: kernel buffers, in some cases extra kmap.
* ITER_FOLIOQ: iterate other folios, map them independently using
  kmap_local_folio() and inspect folio sizes/queues.
* ITER_XARRAY: enter a second RCU critical section to traverse another
  XArray, examine other folios and map those folios.
* ITER_DISCARD: advance without copying data.

This is not proof that any one of those kernel iterator types
deadlocks or corrupts data. Rather, the complete ARMv7/HighMem folio
lifetime and cache audit (S2/S3) has never established their safety
inside the SM-T311 ref-free source-folio window.

Conservative source patch, STAGED NOT INSTALLED
----------------------------------------------
In mm/filemap.c:filemap_read_rcu(), under
CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT only, after ext4 checking but
before even probing lockless_read_enabled()/AS_RCU_READ:

    if (!user_backed_iter(iter))
        return 0;

include/linux/uio.h defines user_backed_iter() as exactly
iter_is_ubuf() || iter_is_iovec(). Thus only normal user destinations
can take the optional ref-free path. All kernel buffer/XArray/folioq
destinations return zero and retain the ordinary, folio-referenced
filemap_read() fallback. Both source and destination semantics
remain unchanged when CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT is disabled.

This further reduces experimental scope, but does NOT prove complete
reference-free read safety even for UBUF/IOVEC: pagecache writer
lifetime, PFN generation ABA, concurrently introduced writable mmap,
ARMv7 VIPT/HighMem alias and Normal-zone pressure remain unresolved.

Source and actual ARM32 validation
----------------------------------
Run:

  python3 tools/testing/selftests/smt311/rcu_iterator_scope_g689.py
  bash tools/testing/selftests/smt311/build_verify_no_flash_g686.sh
  bash tools/testing/selftests/smt311/build_verify_release_off_g687.sh

The G689 validator checks 16 actual source conditions across
mm/filemap.c, mm/vma.c, include/linux/uio.h,
include/linux/iov_iter.h and lib/iov_iter.c; rejects ten genuinely
modified source inputs and pins exact output SHA256 of both linked
ARMv7 kernels.

G689 host experimental filemap.o built with
CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y,
CONFIG_PAGE_RCU_FREE=y, CONFIG_DEBUG_FS=y:

  ELF ARM EABI5, mm/filemap.o SHA256
  bca88389eb72ff0db42968487848a8ca7eba4960929fd90337eafe976a130b68

Fully linked host experimental vmlinux SHA256
  9665657548dfa0f2f27966af5821ceaf05cbba3bb977f81e9a1a2e7621d9c558

Fully linked host experimental zImage SHA256
  caebae5ad3ed1fc951f1ed794ad5ca4fb32ea4e8adff716b6d511272389c7e83

Any future G689 source commit must retain the RCU reader fastpath
filter before the first reference-free folio lookup. The compiler
uses a simple inline comparison of iterator type for this check;
there is no extra global mutex, semaphore, or GP introduced.

Lock dependency scope
---------------------
The pagecache reference-free reader takes RCU, disables user faults,
uses local kmap of its source folio, then exits RCU before the
referenced slowpath. The new iterator restriction excludes nested
destination-side XArray RCU traversals from this particular fastpath.
This removes an unanalyzed subgraph; it does not certify a global
mmap_lock -> synchronize_rcu dependency graph as acyclic.

G683's conservative GP for every ext4 shared-maywrite mmap even
with runtime key OFF remains. Do not add an unverified global rwsem
to avoid it. Do not enable no-ref RCU ON on the physical tablet until
S2/S3 lifetime, cache and lock dependencies are proved.

Physical device remains G650, Linux 7.3.0-pjarczak. No host zImage
is a Samsung boot.img, and no code was installed or flashed.
Exactly FOUR kernel executable source paths are STAGED NOT INSTALLED
vs physical G650 source a2fca4558:
  mm/filemap.c    G674/G680/G685/G687/G689
  mm/page_alloc.c G676
  mm/shmem.c      G688
  mm/vma.c        G680/G683
The optional G686 experiment Kconfig fragment is accounted separately.

S0 PASS, S1 ACTIVE, S2 BLOCKED, S3 BLOCKED, S4 BLOCKED,
S5 BLOCKED, S6 BLOCKED, S7 PARTIAL, S8 ACTIVE.
