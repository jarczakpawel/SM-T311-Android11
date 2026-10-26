G702 - linked ARM32 symbol-size audit, G686 EXP vs G700 RCU OFF
================================================================

This is an OFFLINE comparison of two completed host ARM32 links.
It does not evaluate an installed G700 kernel, physical FUSE or an ABI.

Reproduce without compiling::

  python3 tools/testing/selftests/smt311/rcu_linked_symbol_delta_g702.py

The source and machine-readable output are committed under
tools/testing/selftests/smt311/rcu_linked_symbol_delta_g702.py and
Documentation/mm/sm-t311-rcu-linked-delta-g702.json.

The result is 26742/26742 identical globally defined symbol names
and symbol types. 26547/26547 symbols have explicit sizes; 195 are
not assigned sizes by nm -S. Eighteen global symbols have different
linked sizes. Five are FUSE functions even though source under fs/fuse
does not differ between physical G650 source a2fca4558 and G702 HEAD.

FUSE example sizes (G686 EXP -> G700 OFF, bytes):

* fuse_conn_init: 620 -> 596
* fuse_copy_folio: 1944 -> 1928
* fuse_ctl_add_conn: 460 -> 420
* fuse_direct_io: 2340 -> 2344
* fuse_file_io_open: 432 -> 384

Other differences include filemap_read_rcu 1640 -> 8,
rcu_free_sync 96 -> 4, mmap_region 400 -> 356, allocation and
filemap paths, and three data symbols (pageflag_names, kallsyms_names,
kallsyms_offsets). This is consistent with differences in compiled
machine code; it does NOT establish their root causes or functional
equivalence. In particular, identical global names and types cannot
prove struct layouts, exported module ABI, ioctl behavior, FUSE
mount correctness, device HAL contracts, or physical speed.

G700 BOOT SHA256 remains
aec9655acc727ab01d0bde3711414f9606c58c5fdcafb6ae5c52a2d3346edc7f.
It is HOST ONLY / DO NOT FLASH. Physical BOOT G650 remains the
last recorded working image. Current p9/p10 hashes and recovery boot
qualification remain unknown. The actual deployed G650 compiles
SM_T311_RCU_HOT4K_EXPERIMENT=y and PAGE_RCU_FREE=y; its runtime
static key was not read. G700 compiles both features out and preserves
DEBUG_FS=y. G692 activation refusal applies to new STAGED experiment
code only, not the physically installed G650.

The exact four runtime source differences since G650 remain:
mm/filemap.c, mm/page_alloc.c, mm/shmem.c, mm/vma.c.
G702 modifies no executable kernel C code and executes no hardware
diagnostics, recompilation, replayed old regression or flash.

Release verdict: S6 BLOCKED until physical boot/recovery identity,
owner-attended rollback, and real device functional qualification.
Reference-free RCU ON S2/S3/S4 also remain BLOCKED.
