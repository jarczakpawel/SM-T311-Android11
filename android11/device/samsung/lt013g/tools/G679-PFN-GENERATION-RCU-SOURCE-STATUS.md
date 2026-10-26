# G679 — SM-T311 real-source folio PFN provenance and RCU lifetime

Source-only audit committed in kernel `35548abfd`: `tools/testing/selftests/smt311/rcu_untagged_pfn_g679.py` checks 24 actual bounded C source conditions and rejects 9 actual altered source controls. Machine/plan record kernel `7da59bec0`.

The unmarked initial folio A may be freed/reused while a ref-free filemap reader remains in rcu_read_lock. If a new folio B reuses the same physical address and reader observes B's PG_rcu_free, B itself must remain protected through that reader's grace period. The source's raw pointer comparison xas_reload is not an allocation-generation comparison. G674 G674 inode eviction protection, allocator tagged deferred final free, migration XArray ref freeze, large split exclusion (order-0 filter), hwpoison grace/quarantine were inspected. **No full ARMv7 object-lifetime or all-writers proof.**

No executable change in G679. Only `mm/filemap.c` G674 and `mm/page_alloc.c` G676 remain STAGED NOT INSTALLED vs physical G650. S0 PASS S1 ACTIVE S2–S6 BLOCKED S7 PARTIAL S8 ACTIVE. No physical no-ref RCU ON, BOOT flash, backups, nighttime audio/voice/camera/PIN/OTG actions. GitHub owner-only.
