# G673 — S0/S1 Linus/Kiryl 18 commits physically compiled, no-ref OFF

User priority is Linus Torvalds / Kiryl Shutsemau page-cache refcount avoidance, not repeated archive parsing.

Provenance: kernel Git commit eb1ca383a adds a source/ancestry gate (18/18 imported October 2026 commits from 05362c1a7 through 016e91411 are all ancestors of G650 a2fca4558, 13 source conditions PASS, four changed-source negatives rejected). Kernel machine ledger e67890756 preserves S2/S3 BLOCKED. Project report G673_LINUS_KIRYL_18_PATCH_PHYSICAL_INTEGRITY_20261020.md; updated S8 P0.

Live SM-T311 /proc/config.gz confirms PAGE_RCU_FREE=y, SM_T311_RCU_HOT4K_EXPERIMENT=y, PREEMPT_RCU=y, HIGHMEM=y, SMP=y. Compiled presence is NOT active RCU no-ref: static key default FALSE; debugfs switch currently not read directly. No proof of allocator old-unmarked same-address folio generation ABA or ARMv7 cache/HighMem safety; do not enable feature without S2/S3 PASS. G665 genuine native XArray C test remains UNRUN (missing liburcu-dev).

Physical Android11, kernel 7.3.0-pjarczak, Enforcing, RIL running, SIM LOADED, BOOT still G650. No compiled Android/kernel code change, no new image/flash or backups, no late-night audio/voice/camera/PIN/OTG test.

S0 PASS, S1 ACTIVE, S2 BLOCKED, S3 BLOCKED, S4 BLOCKED, S5 BLOCKED, S6 BLOCKED, S7 PARTIAL, S8 ACTIVE. S7 means archived-conversation research/deployment signoff, S8 rolling board; GitHub user-only.
