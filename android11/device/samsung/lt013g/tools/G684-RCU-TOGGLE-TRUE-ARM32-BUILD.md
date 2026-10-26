# G684 — static key OFF→ON cannot safely skip per-mmap writer grace

Kernel commits `9e2c019af` test/model/JSON/RST and `d86ebe14d` plan. Real 3-actor herd7 LKMM: writer who saw OFF skips own GP while toggler switches ON and itself waits GP — Sometimes 1/8 mixed observation; every writer waits GP — Never 0/8; OFF skip with smp_mb — Sometimes 1/8. Therefore do not add `if (!lockless_read_enabled()) skip GP` without a separately verified outstanding-mmap writer drain/activation protocol. Real source checks 18/18 PASS, 8/8 changed-source mutants detected. Models do not prove observed UAF or global safety.

**CRITICAL COMPILE CORRECTION:** previous G674 build directory had CONFIG_DEBUG_FS OFF, so SM_T311_RCU_HOT4K_EXPERIMENT OFF. Its ARM32 mm/vma.o never compiled the guarded G680/G683 code. G684 recompiled mm/vma.o, mm/filemap.o and mm/page_alloc.o under actual EXPERIMENT=y, PAGE_RCU_FREE=y, DEBUG_FS=y g633 build: all ARM EABI5, `synchronize_rcu` relocation in vma.o. SHA256 and build path in G684 kernel JSON/RST.

No new executable G684 patch or full boot image. Three source paths STAGED NOT INSTALLED vs physical G650: mm/filemap.c, mm/page_alloc.c, mm/vma.c. Normal-mode ext4 mmap GP overhead even with static key OFF still unresolved; S0 PASS, S1 ACTIVE, S2–S6 BLOCKED, S7 PARTIAL, S8 ACTIVE. No backup, flash, audio/camera/phone/PIN/unsafe OTG. GitHub published by owner only.
