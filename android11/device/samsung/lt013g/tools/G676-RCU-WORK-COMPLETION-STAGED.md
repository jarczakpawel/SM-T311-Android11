# G676 — SM-T311 RCU grace/workqueue completion, STAGED

Kernel Git `56c7fba9d`: in mm/page_alloc.c:rcu_free_sync() use `flush_rcu_work(&r->rwork)` instead of `flush_work(&r->rwork.work)`. Kernel/workqueue.c:queue_rcu_work registers a pending RCU callback via call_rcu_hurry before the ordinary embedded work has been queued; flush_rcu_work specifically waits through rcu_barrier if pending and then flushes its work. Ordinary flush_work does not provide this callback-completion contract.

Host gate `tools/testing/selftests/smt311/rcu_work_completion_g676.py`: 16/16 source checks PASS, five modified-source negative variants rejected. Genuine ARM32 `mm/page_alloc.o` compile PASS with SM-T311 defconfig; SHA `10db37874994841e88c7da49f870d234a7a9d946b9cce03cabf02a89204fd63f`. G660/G661/G666/G667/G673/G674/G675/G676 regression PASS. Kernel machine status/plan `83d4b68de` now records **exact two** staged runtime files: G674 `mm/filemap.c`, G676 `mm/page_alloc.c`.

Both changes are **STAGED NOT INSTALLED**. Physical BOOT remains G650 `7.3.0-pjarczak`; no ref-free RCU ON, no full signed BOOT, no flash. RCU lifetime ABA, ARMv7 cache/HighMem and queue backlog remain unproven. S0 PASS, S1 ACTIVE, S2–S6 BLOCKED, S7 PARTIAL, S8 ACTIVE. No backup/audio/camera/calls/modem PIN/night unsafe OTG. GitHub only the owner.
