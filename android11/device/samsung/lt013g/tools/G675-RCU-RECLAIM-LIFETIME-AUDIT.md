# G675 SM-T311 — source-only RCU tag clearing and reclaim pressure audit

G674 mm/filemap.c inode-exit tag preservation remains STAGED, NOT INSTALLED. No further executable change in G675.

Source audit kernel commit 18b0cb79d: 22/22 source guards PASS, 7/7 altered source negative controls rejected. Explicit folio_clear_rcu_free sites are single/batch filemap deletion, guarded off under CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT, plus allocator's sole __folio_clear_rcu_free before rcu_free_defer. Migration does not propagate PG_rcu_free to the destination and uses ref freeze/XArray locking on source; tagged old source relies on deferred recycling. HWPOISON paths synchronize RCU. Not a full ARMv7 proof.

2MiB soft tag-mark limit is not a hard cap on deferred queue size: a hypothetical 1536 earlier-tagged folios retired together could occupy 6MiB before GP. Read-only actual SM-T311 snapshot: Normal 76850 free 4KiB pages, Normal high=1350, HighMem 294 free below low=314, NR_FREE_RCU=0 while no-ref reader remains default OFF. No physical eviction stress has been run.

Machine stage commit 6224e614d preserves S0 PASS, S1 ACTIVE, S2–S6 BLOCKED, S7 PARTIAL, S8 ACTIVE. Physical G650 kernel 7.3.0-pjarczak, Enforcing, RIL running, SIM LOADED. No flash, backups, audio, calls, PIN/PUK or camera tests. GitHub user-owned.
