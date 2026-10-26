# G707 - exact ARMv7 page->flags metadata layout; G700 RCU OFF release

- Real generated G686 EXP vs G700 RCU-OFF `bounds.h` and `.config`, validated by ARM32 compiler `_Static_assert`: 32-bit page flags, internal flag count 22 vs 21, zone width 2 each, MGLRU generation width 3 each, MGLRU reference width 2 each, unused bits 3 vs 4. Metadata high-bit offsets unchanged: zones 30, LRU gen 27, LRU refs 25.
- Both `CONFIG_MEM_ALLOC_PROFILING=n`, so the extra free bit does not enable allocation-tag compression or add runtime allocation tracking. No actual FUSE/Android ABI update required from this specific difference; public `KPF_MLOCKED` mapping already source-verified G706.
- New G707 kernel checker `tools/testing/selftests/smt311/rcu_pageflag_budget_g707.py` passed 22/22 source/config/compiler checks and four negative controls; no rebuild or executable source change.
- Physical last verified G650 remains, G700 full host-only 8MiB BOOT SHA256 `aec9655acc727ab01d0bde3711414f9606c58c5fdcafb6ae5c52a2d3346edc7f` DO NOT FLASH without current p9/p10 SHA and verified safe recovery. No G707 device modifications.
- S0 PASS S1 ACTIVE S2-S6 BLOCKED S7 PARTIAL S8 ACTIVE. Next G708: RCU folio lifetime/PFN ABA/reader-writer S1-S3 not redundant FUSE audits. All later reports and handoffs: ZROBIONE / W TOKU-BLOKADY / DALEJ / FIZYCZNIE NA TABLECIE and complete S0-S8. GitHub owner only.
