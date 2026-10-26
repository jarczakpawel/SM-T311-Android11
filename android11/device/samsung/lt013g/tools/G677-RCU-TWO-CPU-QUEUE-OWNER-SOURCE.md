# G677 SM-T311 — two-CPU deferred RCU ownership model, source-only

Kernel Git `9fae784a6` added source-bound Python BFS for four tagged folios split across two per-CPU RCU pending/batch lists, callback, RCU work, expedite and drain. Results: 10,886 reachable abstract states; 13 source assertions PASS, four actual mutated-source controls rejected, three missing-GP counterfactual models detected nonempty unsafe frees. No lost/double ownership in the baseline **SC model**.

This is NOT ARMv7 LKMM, real concurrency schedule, UAF reproduction or memory-pressure proof. Kernel metadata Git `69685363f` keeps S0 PASS, S1 ACTIVE, S2–S6 BLOCKED, S7 PARTIAL, S8 ACTIVE. No new executable kernel diff in G677; **only two STAGED NOT INSTALLED runtime changes** vs physical G650: G674 `mm/filemap.c`, G676 `mm/page_alloc.c`.

No new BOOT linked/signed/flashed. Physical SM-T311 G650 `7.3.0-pjarczak`, Android boot completed, Enforcing, RIL running, SIM loaded at last passive check. No backups, audio/camera/phone/PIN/OTG nighttime tests; GitHub owner-only.
