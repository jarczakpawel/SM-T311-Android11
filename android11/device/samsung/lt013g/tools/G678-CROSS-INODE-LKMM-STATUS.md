# G678 SM-T311 — real herd7 LKMM, cross-inode folio ABA (source/model only)

Kernel commits `12d1713cf` and `6ca2001cf` add four new C litmus files and a real `herd7 7.58` runner with the Linux kernel memory model. Same physical-pointer abstraction with test-only generation oracle: without RCU grace Sometimes 6/318, with GP Never 0/71, with just `smp_mb()` Sometimes 4/233, without reader `rcu_read_lock()` Sometimes 4/233. 12/12 source anchors PASS, exact output and hashes recorded. RCU grace and reader critical section are essential IN THIS MODEL.

This does NOT prove any physical UAF or global absence thereof; LKMM does not model all XArray nodes, actual PFN lifetime, Cortex-A9 VIPT or HighMem, nor allocator under pressure. S0 PASS / S1 ACTIVE / S2–S6 BLOCKED / S7 PARTIAL / S8 ACTIVE.

No executable kernel source modification in G678. G674 `mm/filemap.c` and G676 `mm/page_alloc.c` are the ONLY two executable source differences from physical G650 a2fca4558 and both remain STAGED NOT INSTALLED. No new image, no flash, no backup, no audio/voice/camera/PIN/OTG stress. GitHub solely by owner.
