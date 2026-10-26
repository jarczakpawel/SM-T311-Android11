G706 - ARMv7 internal pageflags versus stable kpageflags
======================================================

Samsung SM-T311 Linux 7.3.0-pjarczak, G686 experiment versus G700
RCU OFF+DEBUG_FS, 2026-10-21.

Reproduce with existing host build outputs, no compilation or ADB::

  python3 tools/testing/selftests/smt311/rcu_pageflags_layout_g706.py

The checker uses the original kernel source enum pageflags plus the
exact G686/G700 .config preprocessed by arm-linux-gnueabihf-gcc -E.
It independently inspects compiled ARM32 vmlinux symbols and code.
The machine report is sm-t311-rcu-pageflags-layout-g706.json.

There are 17/17 genuine predicates PASS and 4/4 deliberately altered
input controls detected.

Actual pageflag enum values:

* G686 EXP: PG_dropbehind=19, PG_rcu_free=20, PG_mlocked=21,
  __NR_PAGEFLAGS=22.
* G700 OFF: PG_dropbehind=19, PG_rcu_free absent, PG_mlocked=20,
  __NR_PAGEFLAGS=21.

PAGE_RCU_FREE selects an internal flag slot, so removing it changes
PG_mlocked and the internal flags/layout budget. mm/debug.c asserts
its pageflag_names table has __NR_PAGEFLAGS+1 records; actual full
linked ARM32 data symbol sizes are 184 bytes (G686) and 176 bytes
(G700), precisely one 8-byte entry. The linked stable_page_flags
function has the same 448-byte length in both configurations; that
alone does not prove identical behavior or implementation.

The ARM disassembly of fuse_copy_folio contains a 0x200000 mask for
the G686 profile and 0x100000 for G700; the source correctly uses
folio_test_rcu_free and other flag accessors rather than a hardcoded
G686 flag number. This is an expected compile-time difference.
It is not evidence of a FUSE failure or physical performance gain.

The stable userspace interface is distinct from these internal flag
positions. include/linux/kernel-page-flags.h defines KPF_MLOCKED=33,
and include/uapi/linux/kernel-page-flags.h defines KPF_HWPOISON=19.
fs/proc/page.c maps internal PG_mlocked to KPF_MLOCKED by symbolic
names in stable_page_flags(). There is no new KPF_RCU_FREE UAPI bit.
This establishes the source-level mapping, NOT an actual G700 device
/proc/kpageflags read or full HAL ABI verification.

Remaining risks: a separately compiled third-party kernel module
relying on a different internal struct/layout/bit index, capacity
changes in page-flags allocator metadata, or subtle ARMv7 lifetime
and cache ordering. These require their own evidence. The kernel
continues to have CONFIG_MODULES=y but current host builds contain
no loadable .ko files and CONFIG_MODVERSIONS=n.

The G700 HOST ONLY full 8MiB BOOT SHA256 remains
aec9655acc727ab01d0bde3711414f9606c58c5fdcafb6ae5c52a2d3346edc7f,
not installed. Physical BOOT G650 still has not been replaced.
No full rebuild, Android change, flash, reboot/TWRP, backup or
night sound/call/PIN testing was performed during G706.

S0 PASS, S1 ACTIVE, S2-S6 BLOCKED, S7 PARTIAL, S8 ACTIVE.
G707: calculate the actual 32-bit pageflag metadata bit budget,
allocator tag dependencies and implications of the one freed bit;
continue S1-S3 RCU proof and physical S6 recovery gates.
