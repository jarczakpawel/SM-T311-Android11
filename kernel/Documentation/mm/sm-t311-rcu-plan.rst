SM-T311 / G653 — PLAN WDROŻENIA LINUSA I KIRYLA
================================================

THIS FILE MUST BE READ AT THE START OF EVERY SAMSUNG PROJECT WORK SESSION.
The full Polish owner-requested plan is in the tracked project ledger:
linux-7.3-port/SMT311_LINUS_KIRYL_PLAN_G653.md.

Mandatory report, repeated on EVERY subsequent SM-T311 work report:
  DONE: what is completed, evidence and commits.
  ACTIVE/BLOCKED: what is unresolved and unsafe.
  NEXT: the specific following task, and REPEAT THIS RULE in the next report.
  PHYSICAL TABLET: actual BOOT SHA, Android, RCU runtime OFF/ON state.

TARGET: Samsung SM-T311 ONLY, Exynos4212 dual Cortex-A9, 32-bit ARMv7
HighMem, Android 11, kernel 7.3.0-pjarczak.

S0 BASELINE [PASS]:
  Physical G650 BOOT SHA256:
  2d477816fef0dbe2f41ca30747201dbe773e3dc3e710348c118226ee2999b7a3.
  Android boot_completed=1, SELinux Enforcing, RIL/SIM, RCU compiled but OFF.
  Later kernel HEAD may include TEST-ONLY commits; distinguish from BOOT.

S1 SOURCE AUDIT [ACTIVE]:
  Inventory every XArray removal/replace/migrate/truncate/split, inode
  eviction, hwpoison, deferred free; read flag, i_size, reread slot and
  mapping checks on the ARMv7 reader. Linus mark-on-remove differs from
  Kiryl mark-HOT-while-referenced; do not blindly merge their writers.
  Existing 14 source structure checks DO NOT establish LKMM correctness.
  GATE: concrete reader/writer interleaving matrix + source locations.

S2 ARMv7 FOLIO LIFETIME AND ABA [BLOCKED]:
  LKMM/herd7 modeling of reader XArray lookup, flag, barriers, size,
  reload, migration/free and RCU grace period. PREEMPT_RCU + pagefault
  disabled for bounded 32KiB read. No runner/no proof => NOT PASS.
  GATE: demonstrated absence of use-after-free and stale data races.

S3 HIGHMEM SAFETY [BLOCKED]:
  kmap_local_page mappings, Cortex-A9 cache aliases, TLB, DMA coherency,
  2MiB deferred backlog, 4MiB Normal reserve, real usage and memory
  pressure. G646 observed 54 HighMem/47 Normal hot ext4 boot samples,
  not an actual ON hit ratio or a reason to remove guards.
  GATE: memory, cache and folio lifetime evidence without regression.

S4 LIMITED PHYSICAL RCU ON [BLOCKED ON S2/S3]:
  Only if previous gates PASS; timed own-file pread / rename / truncate /
  mmap write / punch-hole / reclaim, errors and RCU queue monitoring.
  Stop on kernel oops, memory corruption, stalls, lost radio or low RAM.
  Never activate ON during owner absence before safety gates close.

S5 REPRODUCIBLE OFF/ON A/B [BLOCKED ON S4]:
  >=10 repetitions ABBA/BAAB, 1/2 Cortex-A9 cores, temperature, energy,
  PMU and real app launch/UI/FrameStats p50/p95/p99.
  Define benefit >=3% on a real workload without p99, stability,
  thermal, memory or power regression. Never cherry-pick one benchmark.

S6 DECISION [BLOCKED ON S5]:
  Enable default ON only after all safety and real-device benefit gates
  PASS. Otherwise retain tested G648/G650 low-overhead RCU OFF path.
  If evidence is inconclusive, maintain optional developer-only switch.

S7 ALL-CONVERSATION RESEARCH / SAFE PHYSICAL DEPLOY SIGNOFF [PARTIAL]:
  Every accepted change tracked in Git kernel AND Android, reproducible
  source/test scripts and physical SHA release manifest. Guarded signed
  BOOT-only TWRP flash only for validated binary changes, with boot1,
  EGL/Mesa/FUSE/modem/GPS smoke testing. GitHub is NOT an S7/S8 stage; OWNER ONLY publishes GitHub.

S8 ROLLING NEXT-WORK BOARD / HANDOFF [ACTIVE]:
  Canonical ledger SMT311_S8_CONTINUITY_BOARD_G670_20261020.md lists all
  unresolved work. Owner priority from G673: Linus/Kiryl RCU S1/S2/S3
  FIRST, archive S7 secondary. Follow S0-S8 in each report.

G701 COMPILED DEBUGFS VS RUNNING ANDROID DEBUGFS MOUNT:
  Passive physical G650 Android shell uid2000 Enforcing:
  /proc/config.gz DEBUG_FS=y, ALLOW_ALL=y, proc/filesystems
  has debugfs, /sys/kernel/debug exists but is EMPTY and
  labeled sysfs in shell view; mount output has no mounted
  debugfs or tracefs, /proc/self/mountinfo is inaccessible.
  Existing userdebug build is not SELinux permissive or root.
  Host Kconfig G700 and physical-profile-matched G686 both
  retain DEBUG_FS=y and ALLOW_ALL=y while G700 disables
  PAGE_RCU_FREE and SM_T311_RCU_HOT4K_EXPERIMENT; matching
  26742 globals vs G687 DEBUG_FS=n 26614. Android device
  vendor init sources have no automatic debugfs mount;
  existing narrow init debugfs_wifi_tracing policy unchanged.
  This is ONE unprivileged G650 snapshot, not a physical
  G700 boot. /proc/partitions and p9/p10 sysfs metadata
  are also unreadable to shell, S6 still BLOCKED.
  G701 source/bin/recorded physical gates 19/19 PASS and
  9/9 malformed in-memory inputs rejected. No adb writes,
  no auto mounts, SELinux widening, flash or backup.

G700 RELEASE RCU OFF WITH DEBUG_FS PRESERVED:
  Starting from the physically G650-matched experiment Kconfig,
  G700 disables ONLY SM_T311_RCU_HOT4K_EXPERIMENT and
  PAGE_RCU_FREE while retaining CONFIG_DEBUG_FS=y.
  Complete linked host ARM32 vmlinux, zImage, 3g8 DTB PASS,
  7.3.0-pjarczak unchanged. G700 all 26742 shared globally
  defined symbol names retained vs 26614 on G687 DEBUG_FS=n.
  Actual mmap_region 89 ARM words, NO experimental RCU GP;
  shmem reader 230 words. 8MiB G700 HOST BOOT SHA
  aec9655acc727ab01d0bde3711414f9606c58c5fdcafb6ae5c52a2d3346edc7f.
  G700 21/21 source/config/ELF/boot positive checks, 9/9
  corrupted cases rejected. This is NO physical A/B result,
  live p9/p10 hashes remain inaccessible; never flash
  unverified G700. S6 BLOCKED.

G699 HOST BOOT/TWRP IDENTITY AND PHYSICAL READBACK BLOCK:
  Historical TWRP 8MiB host image matches SHA f818c29f...,
  valid boot v0 SHA1 ID and correct recovery ramdisk load addr
  0x11000000. Historical host G650 BOOT SHA 2d477816...
  and new G696 RCU OFF host candidate SHA ba9aed97...
  independently verified. Android shell uid2000/SELinux
  cannot read live p9/p10 hashes or sizes. Legacy TWRP
  dw_mmc aliases not visible in Android shell, not evidence
  original TWRP is broken. Project host guard 24/24 and
  10/10 negative controls, kernel consumer 18/18+6/6.
  Manual read-only true TWRP checker SOURCED but NOT RUN,
  and no writes/reboot/backups/flash. S6 remains BLOCKED.

G698 CROSS-REPO ANDROID/KERNEL USERSpace GNSS/RIL/DAC/SELINUX:
  Source+one passive physical G650 check follows Exynos DTS
  samsung,tab3-bcm4752 -> kernel GPS_PWR_EN/value attr 0664,
  Android GNSS legacy init and gps.xml /dev/ttySAC1, common
  ueventd + file_contexts + genfs_contexts and explicit SELinux
  access for legacy GNSS domain. RIL XMM6262 DTS/config,
  /dev/umts_boot0 and /dev/umts_ipc0 system:radio 0660
  labels, stock libsec-ril service, BinderFS/ttyS0 and
  restart treatment. On G650 Android GPS legacy launcher,
  GNSS HAL, cpboot, rild, audio-RIL all running, SIM LOADED.
  Unprivileged shell may not read GPS_PWR_EN sysfs due to
  SELinux and DOES NOT prove GPS domain read failure.
  20/20 actual source + recorded G650 state checks PASS;
  9/9 modified source/observation cases rejected.
  NO actual GNSS fix, modem call, USB stress or new G696
  RCU-OFF boot. S6 remains BLOCKED. No executable changes.

G697 STATIC BUILT-IN DRIVER AND ELF SYMBOL COMPATIBILITY GATE:
  Physical G650 /proc/modules has 0 lines in one passive snapshot,
  no *.ko files seen in accessible Android standard paths;
  CONFIG_MODULES=y but CONFIG_MODVERSIONS=n, both full ARM32
  host profiles have ZERO =m symbols in .config and no .ko files.
  G697 17/17 actual source/binary/single snapshot gates PASS,
  9/9 mutated input controls rejected. Experiment vmlinux
  26742 globally defined names, release-OFF 26614, all 26614
  names are shared; 128 removed, zero added. Missing names
  include debugfs/diagnostics plus timekeeping helpers; lost
  debug ability may matter, not proven harmless. All 12 pinned
  core hardware Kconfig features remain built in. XMM6262
  29, DWC2 112, DW MMC 8, Broadcom WiFi 183, ALSA 295,
  Exynos 151 globally defined family names retained.
  NO proof of Android ioctl/sysfs/SELinux, firmware runtime,
  bootability or physical modem/GNSS operation. G696 full
  boot remains HOST-ONLY and DO-NOT-FLASH, S6 BLOCKED.

G696 ACTUAL OFFLINE 8MIB BOOT FORMAT, NO FLASH:
  Reused pinned historical G650 exact 8MiB HOST baseline SHA
  2d477816..., preserved Android boot v0 2048B pages, all load
  addresses and SHA-pinned 786423B ramdisk unchanged. Source G650
  repacker roundtrips FULL 8MiB byte-for-byte. Linked release
  OFF zImage SHA 1c2dd2... with independently built and exact
  old-known Exynos4212 3g8 DTB SHA 6fb3e770..., regenerated v0
  kernel length/sha1 ID and 8MiB padding. G696 complete raw BOOT
  candidate SHA ba9aed97fe7a028fc9ff5b540c916b251a7cd064a9
  fb4055b5eb3f7af254204; host project artifacts/G696-RCU-OFF
  -HOST-ONLY, NO INSTALL. Project generator six corruption
  checks; independent kernel selftest 24/24 positive guards and
  9/9 altered-input guards. Config diff 29 options, 12 key
  hardware symbols retained. Physical BOOT p9 SHA NOT freshly
  read, TWRP preflight and RIL/firmware module ABI NOT verified;
  Android makefile still pins historic G541, not G696. This is
  NOT a bootable device certification or owner-supervised flash.
  S2/S3/S6 BLOCKED, G650 still running.

G695 TRUE PHYSICAL /proc/config.gz AND EXPERIMENTAL ARM32 COMPARATOR:
  Real physical G650 /proc/config.gz has 6794 lines, SHA
  ef893db0..., and CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y,
  CONFIG_PAGE_RCU_FREE=y, CONFIG_DEBUG_FS=y. G694 G650 SOURCE
  compiled under RELEASE-OFF was matched C profile comparison,
  NOT direct binary/boot comparison to the deployed tablet!
  G695 compares ALL 6794 lines physical against host EXP: ONLY
  3 Rust toolchain version metadata fields differ. Changing
  those three recreates exact observed physical config SHA.
  Compiled G650 SOURCE with same host experimental CONFIG and
  GCC, four real ARM32 objects, and compared to current EXP
  and release OFF. New EXP mmap_region 100 vs G650 89 words
  and +R_ARM_CALL synchronize_rcu, even with G692 runtime ON
  setter fail closed. New RELEASE OFF mmap_region 89 words,
  no new GP; all three shmem_file_read_iter 230 words.
  C/ARMv7 evidence 19/19 real gates and 9/9 tampered controls.
  Physical runtime static key NOT directly readable by uid shell.
  No on-device p99, no physical benchmark, S2/S3 BLOCKED,
  no flash and no new runtime C code.

G694 REAL MATCHED G650 VERSUS RELEASE-OFF ARM32 OBJECT ASSEMBLY:
  Independently compiled exact physical source G650 a2fca4558
  with identical defconfig, same Debian GCC 16.2 and fixed UTS
  provenance as current release OFF, then used nm+objdump -dr on
  mm/filemap.o and mm/shmem.o. filemap_read G650 and current
  BOTH 1236 bytes, 309 identical ARM words, zero RCU stub calls:
  G687 source cleanup has no EXTRA executable-code benefit
  here; compiler already removed the all-zero call in G650.
  shmem_file_read_iter G650 976 bytes / 244 instructions and
  a real R_ARM_CALL filemap_read_rcu; current release OFF
  920 bytes / 230 instructions and NO RCU stub call after
  G688: -56 bytes / -14 ARM instruction words. Matched
  assembly reduction is NOT device throughput or p99 evidence.
  Test script rcu_off_object_diff_g694.py 17 actual compiler/C
  checks and 7 mutated comparisons. S2/S3 still BLOCKED,
  G692 ON fail-closed, NO physical flash.

G693 REFERENCED XARRAY PROTOCOL + ACTUAL NORMAL-ZONE BUDGET:
  Existing kernel filemap_get_entry obtains folio_try_get() under
  RCU after xas_load(), followed by xas_reload() / folio_put retry.
  A reference pins an incarnation during copy; pointer equality
  alone cannot. G693 source predicates 16/16 PASS and ten actual C
  mutation controls REJECTED; bounded two-task SC reader/writer model:
  unpinned old->new ABA 39 states with 2 invalid recycled visits;
  referenced copy 70 states, zero invalid copy visits including
  benign re-acquisition of NEW B after same address reuse. NOT a
  full LKMM or physical proof. Repaired outdated G659 source test
  adjacency after G680/G685 and reran four actual herd7 outcomes.
  Physically observed passive G650 Normal zone managed 203153 pages
  (793.57 MiB), free 74692 (291.77 MiB); HighMem free 308 below
  high 500 pages. Hypothetical all-folio 10% Normal deferred free
  would hold ~79.36 MiB, not an observed runtime queue. Avoid
  whole-filecache GP defer before memory costs and lifetime proof.
  G692 fail-closed ON guard retained; no runtime source change,
  no flash, S2/S3 BLOCKED.

G692 FAIL-CLOSED EXPERIMENTAL DEBUGFS RCU ACTIVATION (STAGED):
  Source G659/G691 old-untagged/new-tagged same PFN ABA prevents
  safe no-ref ON signoff. G692 mm/filemap.c rejects debugfs write
  val=1 with -EOPNOTSUPP, retains val=0 OFF and val>1 -EINVAL.
  SM-T311 static key initialized FALSE. Upstream referenced
  filemap_get_entry safely uses folio_try_get then xas_reload
  and retries with folio_put if pointer changed; this is NOT the
  same lifetime guarantee as existing unpinned experimental reader.
  Full ARM32 experiment vmlinux disassembly shows nonzero val
  dispatch to MVN r0,#94, BX lr. Release RCU OFF binary unaffected.
  18/18 actual source gates, 12/12 changed C, 7/7 actual ARM
  setter disassembly and 2/2 altered disassembly controls PASS.
  Existing G684/G685 ON/OFF LKMM hypotheticals remain relevant
  but ON can no longer be requested through this compiled setter.
  Never claim this interlock repairs ABA or makes ON safe. S2/S3
  BLOCKED and no BOOT flashed.

G691 UNTAGGED OLD FOLIO -> TAGGED REUSED PFN ABA (ABSTRACT BLOCKER):
  filemap_read_rcu_lookup raw xas_load pointer P precedes the
  PG_rcu_free test, xas_reload pointer equality and mapping check.
  An original UNTAGGED folio can enter normal buddy reuse at final
  put without an RCU GP; a newly reused same physical P at same
  inode/index may be tagged by filemap_mark_hot before original
  reader tests its stale raw pointer. Same-PFN pointer and mapping
  checks then pass in a bounded conceptual schedule, despite the
  changed generation. Source audit 19/19, 9/9 actual mutations.
  Initial untagged 54-state model witness; tagged original 47-state
  defer-until-grace model no witness; hypothetical atomic generation
  check excludes this witness. Neither physical UAF nor full LKMM
  correctness demonstrated. Do not enable RCU ON, do not claim a
  smp_rmb() is a generation pin. No executable change/flash.
  S2/S3 remain BLOCKED and four source paths remain STAGED.

G690 mmap_lock -> synchronize_rcu SOURCE LOCK GRAPH (HOST DIAGNOSTIC):
  Audited the direct candidate cycle: mmap_region holds mmap
  write lock during EXT4 synchronize_rcu while a /proc maps reader
  inside RCU might wait for mmap_read_lock. With PER_VMA_LOCK=y,
  vma_start_read releases RCU on error before goto fallback;
  fallback_unlock also releases RCU; lock_next_vma reacquires RCU
  only AFTER the sleepable mmap lock helper returns. /proc explicit
  fallback and query paths also drop RCU first. A source-anchored
  bounded scheduler: correct read-side ordering no deadlock in
  26 reachable states; counterfactual lock while holding RCU
  has an ABBA witness among 17 states. 19 actual source predicates
  and nine mutated C controls PASS. No actual physical deadlock
  or global lockdep proof is claimed. S2/S3 BLOCKED; G683 mmap
  GP overhead remains when experimental runtime key OFF.
  No runtime code modifications, no flash.

G689 NO-REF ITERATOR TYPE SCOPE RESTRICTED ON CORTEX-A9 (STAGED):
  Audit found that _copy_to_iter in reference-free RCU batch delegates
  to iterate_and_advance, which also supports ITER_XARRAY with nested
  RCU/XArray folio kmap, ITER_FOLIOQ with extra folio mappings and other
  kernel-side destination types. These combinations are not included
  in the SM-T311 ARMv7 cache and PFN ABA lifetime safety proof.
  Source mm/filemap.c G689 now checks user_backed_iter(iter) inside
  the SM-T311 experiment scope before entering ref-free fastpath:
  only ITER_UBUF and ITER_IOVEC allowed; every other type uses the
  ordinary refcounted buffered read. Actual source checks 16/16 PASS
  and ten true modified-C controls REJECTED. Both full host ARM32
  profiles compile and link; normal RCU-OFF path behavior unchanged.
  This does NOT prove a physical deadlock/UAF nor all user-buffer RCU
  lifetimes or global mmap_lock->GP lockdep. S2/S3 BLOCKED; no flash.

G688 SHMEM RELEASE-OFF HOT ZERO-STUB CALL ELIMINATION (STAGED):
  G687 identified release OFF mm/shmem.c still called the two-
  instruction always-zero ARM filemap_read_rcu stub in every
  shmem_file_read_iter. G688 #elif CONFIG_PAGE_RCU_FREE uses real
  ref-free reader only when available, else retval=0 directly.
  Existing SM-T311 experimental path always uses fallback shmem.
  Rebuilt two full ARM32 host images: release OFF and experiment.
  8/8 source checks, 6/6 actual mutated C controls, 6/6 linked
  objdump checks, 2/2 modified ASM controls PASS; no real speedup
  quantified. Runtime staged sources increase from THREE to
  FOUR: mm/filemap.c, mm/page_alloc.c, mm/shmem.c, mm/vma.c.
  FOUR are NOT INSTALLED vs physical G650. No new RCU ON proof.

G687 RELEASE-OFF ZERO-STUB HOT CALL ELIMINATION (STAGED):
  Compiled clean from tracked default SM-T311 defconfig OFF: full
  ARM32 release vmlinux/zImage 7.3.0-pjarczak. Actual ARM disassembly
  showed a bl to two-instruction filemap_read_rcu MOV r0,#0 / BX lr
  stub in ordinary filemap_read; G687 #elif CONFIG_PAGE_RCU_FREE
  removes this hot call when RCU feature compiled OFF, preserving
  optional experimental call and 64-bit upstream-capable path.
  Separate experimental full vmlinux/zImage comparator proves ext4
  mmap synchronize_rcu relocation exists only when EXPERIMENT=y.
  ARM32 assembly 7/7, source/binary 16/16, 11/11 mutated inputs
  rejected. mm/shmem.c still has release OFF zero-stub call: G688
  candidate. No physical install; S2/S3 still BLOCKED.

G686 HOST FULL LINK FROM TRACKED SM-T311 DEFCONFIG (NOT FLASHED):
  Added arch/arm/configs/sm_t311_rcu_experiment_g686.fragment,
  build_verify_no_flash_g686.sh and rcu_linked_image_gate_g686.py.
  Fresh separate ARM32 build/g686-reproducible-exp-on from tracked
  sm_t311_android11_defconfig + fragment ENABLES EXPERIMENT=y,
  PAGE_RCU_FREE=y, DEBUG_FS=y; fully links vmlinux (ELF ARM EABI5)
  and zImage with exact UTS_RELEASE 7.3.0-pjarczak.
  Explicit LOCALVERSION= fixes earlier accidental '+' SCM suffix.
  Source/output gate 5/5 PASS, seven tampered-input controls rejected;
  all 17 G660–G685 regression gates PASS and S0–S8 plan PASS.
  vmlinux SHA c0a36fb9716c93f44ea4cbba5d370699e2e58f5bf8913baa96fdf1ee22c9786b
  zImage SHA bea602b0cb12d2b8c096e703594211eb0627b5ba1af976dbd4ccb80255f02104
  Host output is NOT a Samsung BOOT or TWRP OTA. No flash.
  No new executable source changes: still exactly 3 runtime paths
  STAGED NOT INSTALLED vs physical G650. New global mmap-writer
  admission rwsem / OFF shortcut NOT implemented: full lockdep and
  RCU reader-drain proof missing. S2/S3 remain BLOCKED, G687 next.

G685 RECHECK NO-REF STATIC KEY INSIDE RCU (STAGED):
  Source revealed key checked outside filemap_read_rcu_batch's RCU
  critical section. A reader can see ON, stall before rcu_read_lock,
  then enter after an OFF toggler's hypothetical GP. Real herd7
  LKMM for that prospective shutdown protocol: old outside-only gate
  Sometimes 1/4, guarded second check inside rcu_read_lock Never 0/6.
  Added narrow CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT key recheck and
  early rcu_read_unlock + fallback before mmap/cache check.
  Source gate 16/16 PASS, eight actual changed-source mutants caught.
  Recompiled mm/filemap.o ARM32 with true EXPERIMENT=y, PAGE_RCU_FREE=y,
  DEBUG_FS=y. This is NOT a full safety proof; current debugfs
  disable does not itself wait GP and current G683 writer GP remains
  unconditional for ext4 VM_SHARED MAYWRITE. Do not apply OFF shortcut
  without future complete writer admission + drain, ON/OFF proof.
  NO flash, 3 runtime paths remain STAGED, S2/S3 BLOCKED.

G684 CRITICAL EXPERIMENT-ON COMPILE VALIDATION + RUNTIME KEY RACE:
  Old g674-inode-aba-safety .config had DEBUG_FS OFF, which Kconfig
  used to disable SM_T311_RCU_HOT4K_EXPERIMENT and PAGE_RCU_FREE.
  Therefore earlier mm/vma.o ARM32 builds did NOT compile the gated
  G680/G683 code. G684 correctly built ALL three runtime objects
  mm/vma.o, mm/filemap.o, mm/page_alloc.o as ARM EABI5 using g633
  config with EXPERIMENT=y, PAGE_RCU_FREE=y, DEBUG_FS=y; GP
  relocation confirmed in vma.o. Actual three-actor herd7: OFF
  writer skips own GP while key toggle does GP Sometimes 1/8 bad,
  each writer own GP Never 0/8, OFF writer smp_mb no GP Sometimes
  1/8. Toggle GP cannot stand in for unsynchronized in-flight VMA
  writer. 18/18 source gates, 8/8 mutants; NO executable change,
  three previous paths STAGED NOT INSTALLED, S2/S3 BLOCKED.
  G685 priority: prove activation-drain protocol if OFF cost must
  be removed, or leave conservative G683 staged with GP overhead;
  do NOT flash experimental BOOT.

G683 FIX ELIGIBILITY BIT / MMAP COUNTER PUBLICATION RACE (STAGED):
  Former G680 writer skipped synchronize_rcu when mapping_rcu_read()
  tested false. Concurrent folio tagging can publish AS_RCU_READ while
  the reader and writer observe the eligible bit and separate
  i_mmap_writable count in opposite orders on ARMv7.
  Real herd7 models: old gate Sometimes 1/8, unconditional GP on new
  EXT4 shared-maywrite mmap Never 0/8, writer mb-only Sometimes 1/8,
  reader full-barrier counterfactual Never 0/7. 14/14 source checks,
  8/8 changed-source mutations reject. Actual mm/vma.c now waits GP
  for every EXT4 shared-maywrite mmap if CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT;
  non-ext4 excluded per filemap_mark_hot whitelist. ARM32 mm/vma.o PASS.
  WARN: mmap_lock-held synchronize_rcu() now incurs cost even when
  experimental runtime static key is OFF; not measured or approved.
  Source remains STAGED NOT INSTALLED; no new BOOT. S2/S3 BLOCKED.

G682 TWO WRITERS: DO NOT SKIP GRACE FOR EXISTING POSITIVE COUNT:
  Real herd7 v7.58 LKMM 3-thread counterexample: two processes hold
  different mmap_locks but increment the SAME file mapping's writable
  count. Writer B may see the reserved positive count of writer A,
  skip GP, and publish B's VMA before A's GP, exposing an old no-ref
  reader to a writable alias. Skip-GP Sometimes 1/5, both wait GP
  Never 0/5, smp_mb instead Sometimes 1/5. 13/13 C source checks,
  6/6 altered-C controls rejected. Keep G680 conservative per-map
  GP candidate. NO new executable change, no physical tests.
  S2/S3 remain BLOCKED, 3 runtime files STAGED NOT INSTALLED.

G681 MMAP ACCOUNTING/LOCK DEPENDENCIES SOURCE AUDIT:
  20/20 source predicates and 10/10 real changed-source negative
  controls PASS. fork duplicates an existing shared-maywrite VMA,
  VMA split derives from an existing VMA; mprotect changes WRITE
  within MAYWRITE; mremap does not directly increase mapping count;
  memfd rollback is not first new ext4 writable mmap publication.
  The specific no-ref reader does NOT acquire mmap_lock while in
  RCU/pagefault_disable. Not a global lockdep or latency proof.
  No executable source changes in G681; G674/G676/G680 three paths
  remain STAGED NOT INSTALLED, S2/S3 BLOCKED.

G680 WRITABLE MMAP PUBLICATION/CACHE COHERENCY HANDSHAKE (STAGED):
  The original pre-RCU mapping_writably_mapped() test permitted a
  concurrent VM_SHARED may-write mmap to become visible while no-ref
  reader was copying without flush_dcache_folio. Source correction:
  reader enters RCU BEFORE writable-count check and exits RCU on
  rejection; mmap_region for SM-T311 tagged inodes publishes positive
  writable count, completes synchronize_rcu(), then publishes VMA.
  Actual herd7 LKMM Never 0/3 with GP, Sometimes 1/3 for no GP,
  smp_mb instead of GP, or reader precheck before RCU.
  15/15 source guards + 7 mutants; both mm/filemap.o and mm/vma.o
  independently compile ARM32. Possible mmap latency / mmap_lock
  interactions need testing; not full cache proof.
  STAGED NOT INSTALLED now THREE runtime files mm/filemap.c,
  mm/page_alloc.c, mm/vma.c vs physical G650. S2/S3 BLOCKED.

G679 SOURCE-TRACED REAL PFN / UNTAGGED FOLIO PROVENANCE:
  24/24 bounded actual C source conditions PASS; 9/9 mutated
  source inputs rejected. Old unmarked folio A CAN be recycled
  while reader holds RCU; reader may observe new tagged B's bit
  at the reused address. B must survive reader GP if accepted.
  G674 inode-exit tag retention and normal tagged allocator
  deferral cover two concrete early-recycling routes. Migration,
  split order-0 exclusion and hwpoison grace/quarantine were
  checked, but no real object generation is compared by XArray
  reader. NOT a complete proof or physical UAF reproduction.
  S1 ACTIVE / S2-S3 BLOCKED; NO new executable code or flash.
  G674 and G676 files remain STAGED NOT INSTALLED.

G678 REAL LKMM CROSS-INODE ABA GRACE COMPARISON:
  Actual herdtools7 7.58 with in-tree linux-kernel.cfg and FOUR C
  cross-inode folio-generation diagnostic models. Before grace:
  Sometimes 6/318; after grace: Never 0/71; mere smp_mb instead of
  grace: Sometimes 4/233; remove reader RCU critical section:
  Sometimes 4/233. Source binding 12/12 PASS. Model generation is
  an oracle absent from kernel; no ARMv7 D-cache, PFN lifetime,
  complete XArray generation or other writer verification. Hence
  S1 ACTIVE and S2/S3 BLOCKED; NO new executable source or flash.
  G674 and G676 runtime differences remain STAGED NOT INSTALLED.

G677 TWO-CPU QUEUE OWNERSHIP SOURCE/MODEL ONLY:
  10,886 reachable SC states / 4 folios across two per-CPU pending
  and batch lists, callback, expedite and drain; 13/13 source gates
  PASS, four real source mutants and three grace-period model mutants
  rejected. No lost/double owner in the bounded baseline model.
  NOT proof of weak-order ARMv7 RCU semantics or absence of a UAF;
  no physical RCU ON, no executable source change in G677.
  G674/G676 source differences remain STAGED NOT INSTALLED.

G676 RCU_WORK COMPLETION STAGED, NOT INSTALLED:
  mm/page_alloc.c:rcu_free_sync() now uses flush_rcu_work(&r->rwork)
  instead of generic flush_work(&r->rwork.work). RCU callback may be
  pending in call_rcu_hurry() before embedded work reaches workqueue.
  Source checks 16/16 and 5 altered-source controls, ARM32 page_alloc.o
  standalone compile PASS. No linked BOOT, no flash, no RCU ON.
  Executable staged diffs versus physical G650 now BOTH mm/filemap.c
  G674 and mm/page_alloc.c G676. S2/S3 remain BLOCKED.

G674 STAGED, NOT INSTALLED:
  Experimental SM-T311 only: preserve PG_rcu_free on inode eviction,
  preventing same-physical-page B from bypassing grace while an unrelated
  inode's reader may have observed its flag. Kernel source commit
  5a038ac99. ARM32 mm/filemap.o compiled, 14 source checks and seven
  mutations, 75,582 SC schedules, 11 upstream modeled witnesses vs
  zero in patched scenario. NOT real ARMv7 LKMM/physical safety proof.
  Deployed G650 remains from a2fca4558, ref-free RCU DEFAULT OFF.
  Plan checker permits mm/filemap.c diff ONLY as staged candidate
  and rejects any mistaken physical deployment promotion.

STOP: No new backups per user. No SIM PIN/PUK automation. No ringtone,
call, audio or camera during user's sleep. No G641 wake-affine, DMC60
or unverified WIN4/HWC resurrection. Never claim a source-only test
was physically flashed. Never use 32x fewer PMU cache misses as
32x higher real throughput.

Machine-readable canonical progress:
Documentation/mm/sm-t311-rcu-progress.json.
Fail-closed plan checker:
tools/testing/selftests/smt311/plan_check.py.

G654 S2/S3 preparatory facts
---------------------------

Three abstract LKMM litmus files staged, but herd7 absent: S2 still BLOCKED.
The actual i_size_read on ARMv7 SMP uses seqcount, not single 64bit READ_ONCE.
Normal physical headroom (single snapshot) +309.83MiB above high+4MiB
reserve, so Normal resource gate did not impede start in this snapshot;
no ref-free HighMem lifetime/cache coherency proof: S3 still BLOCKED.
See Documentation/mm/sm-t311-rcu-lkmm-g654.rst, and remember to repeat
DONE / BLOCKERS / NEXT / PHYSICAL BOOT / S0-S7 next turn.

G657 actual herd7 availability and observations
------------------------------------------------

herd7 7.58 now runs all FIVE SM-T311 example litmus models using the
local -I <kernel>/tools/memory-model search path for the kernel-specific
.cat, .bell and .def model files. A standard upstream full-fence test
was validated as Never 0/3. Actual local results: message-passing
barrier Never 0/3; missing barrier Sometimes 1/3; abstract ABA
Sometimes 6/66; 32-bit seqcount Never 0/18; no-seqcount-retry Sometimes
3/9. This does NOT close S2: memory lifetime, complete XArray writer
locks, allocator reuse and HighMem safety remain unproved.
See Documentation/mm/sm-t311-rcu-lkmm-g657.rst.
