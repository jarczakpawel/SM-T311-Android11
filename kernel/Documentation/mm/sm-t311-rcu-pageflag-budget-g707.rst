G707 - exact SM-T311 ARMv7 page->flags metadata budget
=====================================================

This is a host-only source/build audit of the actual G686 RCU experiment
and G700 RCU-OFF + DEBUG_FS profiles. No physical kernel was flashed.

To validate existing kernel source and generated Kbuild bounds without
rebuilding the kernel::

  python3 tools/testing/selftests/smt311/rcu_pageflag_budget_g707.py

The checker reads the original include/linux/page-flags-layout.h and
include/linux/mmzone.h, generated bounds.h from both compiled ARMv7
configurations, actual .config files, and allocation-tag source.
It also passes the original page flags layout preprocessor macros
to arm-linux-gnueabihf-gcc with target sizeof(long)=4 and
_Static_asserts covering ten layout properties for each profile.

Results: 22/22 independent static/configuration/compiler checks PASS;
four deliberately inconsistent ARM32 _Static_assert controls FAIL as
expected. Results in sm-t311-rcu-pageflag-budget-g707.json.

Real 32-bit flag layout values:

=======================================  =========  =========
Property                                 G686 EXP   G700 OFF
=======================================  =========  =========
BITS_PER_LONG                            32         32
NR_PAGEFLAGS                             22         21
ZONES_WIDTH                              2          2
LRU_GEN_WIDTH                            3          3
LRU_REFS_WIDTH                           2          2
NODE/SECTION/CPUPID/KASAN widths         0          0
NR_NON_PAGEFLAG_BITS                     7          7
NR_UNUSED_PAGEFLAG_BITS                  3          4
ZONES_PGOFF                              30         30
LRU_GEN_PGOFF                            27         27
LRU_REFS_PGOFF                           25         25
=======================================  =========  =========

CONFIG_PAGE_RCU_FREE=n in G700 removes one low flag bit, leaving
four instead of three available unused bits. It does NOT change the
already capped LRU reference count width (two bits), nor zone/MGLRU
metadata widths and their high-bit positions. Both builds have
CONFIG_FLATMEM=y, CONFIG_HIGHMEM=y, CONFIG_LRU_GEN=y and
CONFIG_LRU_GEN_ENABLED=y, with three zones and two CPUs, no NUMA,
SPARSEMEM, KASAN or last-cpupid storage.

mm/alloc_tag.c contains compression arithmetic using
NR_UNUSED_PAGEFLAG_BITS, so a hypothetical enabled kernel could
address 2^3=8 versus 2^4=16 encoded tag values before accounting for
reserved slots and other constraints. Crucially,
CONFIG_MEM_ALLOC_PROFILING=n in BOTH examined actual profiles:
these values are theoretical ceilings only and are NOT active runtime
tracking capacity. The optional allocator-tag subsystem is not a
reason to enable RCU or to add page flags to this tablet.

The source-only assertion in mm/filemap.c that NR_PAGEFLAGS==22 is
guarded by BOTH CONFIG_PAGE_RCU_FREE and the experimental SM-T311
Kconfig. It does not apply to G700 RCU-OFF, where NR_PAGEFLAGS==21.
This is intentional rather than a build-breaking regression.

The exact existing full 8MiB G700 host BOOT digest is still
aec9655acc727ab01d0bde3711414f9606c58c5fdcafb6ae5c52a2d3346edc7f
and is not installed. No executable kernel/Android code was changed
in G707. No runtime ABI, physical RCU correctness, performance,
BOOT p9 or RECOVERY p10 validation is claimed.

S0 PASS; S1 ACTIVE; S2-S6 BLOCKED; S7 PARTIAL; S8 ACTIVE.
Next G708: return to the core S1-S3 RCU reader/writer and PFN/ABA
lifetime proof, rather than extending already-complete FUSE/page
flag configuration audits. S6 physical recovery gate stays closed.
