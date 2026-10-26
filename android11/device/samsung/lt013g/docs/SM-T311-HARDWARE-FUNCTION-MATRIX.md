# G728 CURRENT — G727 ACTIVE BOOT VERIFIED; USB PC ADB/MTP PASS; OTG HARDWARE TEST NEXT

The owner physically connected laptop USB on 2026-10-21. G727 ARM32 test kernel SHA `dcc7aafe81bb947e9afb4c8d1c4d49bbbeb413f65164c014cb02d392ae57f9bc` was **flashed to physical BOOT p9**, verified by readback, and rebooted. Android boot_completed1, SIM LOADED, RIL running, Enforcing. Laptop ADB works and Android USB MTP connected/configured=true. Fresh G727 MAX77693 current interlock is blocked=0 safe_off=1 detach_edges=0 detach_ack=0 fault_latched=0 block_reason=0 while USB PC connected (EXTCON USB1, HOST0, BOOST OFF). ADB WiFi 192.168.2.238:5555 restored and root independently checked. User asked to disconnect laptop and insert OTG adapter+pendrive for actual USB HOST test. This test is **NOT YET COMPLETE**, so do not mark OTG fixed. Earlier G727 "only staged" entries below are history. Full project `G728_G727_PHYSICAL_FLASH_PC_PASS_20261021.md` and Android `tools/G728-G727-PHYSICAL-BOOT-PC-USB-PASS.md`. S0 PASS, S1-S3 RETIRED, S4 REMOVED, S5 PARTIAL actual USB HOST, S6 PARTIAL TWRP, S7 ACTIVE OTG only (other functions user PASS), S8 ACTIVE.

---

# G724 CURRENT — experimental USB OTG source+test BOOT complete, physical HOST unqualified

SM-T311 actual G650 USB PC mode ADB+MTP **PASS as user confirms**, Wi-Fi ADB stays connected too. Unplugging laptop and connecting OTG pendrive earlier on G650: MAX77693 grounded-ID MUIC attach recognized but USB host external device0, source OTG BOOST CONFIG disabled. Kernel test branch `smt311-g723-otg-muic-return-guard` HEAD G724 6a38a1a6f with MAX77693 boost guard current900 rejection, EXTCON publication error propagation, dedicated defconfig, 17/17 source guard checks and full ARM32 build. G724 BOOT **only /data/local/tmp**, SHA06dc3e3567e5af5cd19acb160eab9bad312486e7504ed044a1c34f5f96931099. G716 release remains immutable and physical G650 kernel. No actual USB OTG VBUS hardware test or disk mount, no flash. Full project `G724_OTG_MAX77693_FULL_SOURCE_RESEARCH_20261021.md`. S0 PASS S1–S3 RETIRED S4 REMOVED S5 BLOCKED G724 physical S6 PARTIAL TWRP S7 ACTIVE OTG (all other user-verified functions PASS) S8 ACTIVE.

---

# G723 CURRENT — rzeczywisty OTG FAILURE, reszta kluczowych funkcji użytkownik potwierdził PASS

User's newest physical report 21 Oct: **voice incoming/outgoing including hangup, SMS send/receive, USB MTP, ADB USB/Wi-Fi, GPS, Developer options, HWC/SystemUI visually are GOOD**. Do NOT recycle historic "broken modem/SMS/UI/MTP" tasks. With pendrive via OTG, physical G650 `/proc/config.gz` USB_STORAGE=y, EXTCON_MAX77693=y, but CHARGER_MAX77693_T311_OTG_EXPERIMENTAL **NOT SET**; MAX77693 MUIC ID grounded attach recognized, yet Android sees internal HSIC modem only, ZERO external USB mass storage. Detailed `tools/G723-OTG-UNPOWERED-PASSIVE-20261021.md` and project `G723_USB_OTG_PHYSICAL_MUIC_BOOST_DIAG_20261021.md`. G723 independent branch ARM32-compiled mux I2C error propagation fix, not installed; G716 untouched. S0 PASS S1-3 RETIRED S4 REMOVED S5 BLOCKED G716 physical S6 PARTIAL TWRP S7 ACTIVE OTG power/host only S8 ACTIVE.

---

# CURRENT G722 — główna tabela stanu funkcji na 21 Oct 2026

Read `tools/G722-RELEASE-AND-ACTIVE-HARDWARE-MATRIX.md` plus project `G722_CURRENT_DEPLOYMENT_AND_BUG_MATRIX_20261021.md` **before** the historical rows below. GNSS G717 installed on real tablet, G721 fix PASS (6s TTFF, hAcc12m), GPS user-closed; old G385/G585 no-fix NOT current. Clean G716 source no-ref RCU port REMOVED, host full 8MiB BOOT PASS, Android rebuilt out/boot.img G716 SHA 5d275fb7977d95e9e3515ff571efe675359e0874e1f8c5aef4b9c925d19182c1, physical only /data/local/tmp inactive, BOOT partition still G650. Full OTA ZIP on host still G541 BOOT and STRICTLY BLOCKED. S0 PASS, S1–3 RETIRED, S4 REMOVED, S5 BLOCKED physical G716, S6 PARTIAL recovery, S7 ACTIVE modem hangup/SMS, USB MTP/OTG mismatch, HWC and ADB WiFi, S8 ACTIVE. No backup or auto PIN. Historical G584/G403/TWRP and GPS tables are evidence of earlier stages, not present deployment.

---

# G632 — 20 October 2026: physical quantitative ARM32 page-cache RCU qualification, NOT ENABLED by default

**Current physical BOOT is experimental G631**, Linux 7.3.0-pjarczak with G626 USB XMM6262 PM hold preserved and latest authentic 2026-10-19 Kiryl/Linus 18/18 RFC + SM-T311 32-bit hot ext4 order-0 4KiB RCU port compiled into kernel. Source Git linux-7.3-port/experimental/G631-arm32-hot4k-rcu-experiment 000e7dfe2 + 41658c9a4; physical G631 boot SHA 9b2e8f78a37ec324f0084ae8ac5307910471c2f1662c6101d10aca02c01de4cd. Original RECOVERY and Android11 SYSTEM/G621 GPS/G623 radio-audio untouched. Experimental runtime RCU fastpath is OFF by default and is still OFF after all G632 testing. Linux SELinux Enforcing, Android boot_completed1, CPBOOT/RIL/audio/GNSS/cameraserver running, MTP applied/configured, nr_free_rcu0, no OOM/crash observed in bounded logs. Signed test rollback to G626 available on tablet `/data/local/tmp/G631-RETURN-G626-BOOT-TWRP-SIGNED.zip`, no backups.

G632 same-boot 24-round paired OFF/ON buffered hotpread test on real SM-T311, 1.4GHz both CPUs, battery temp 31.6C, 12 pairs/case ON faster 12/12: 1t16B +14.86%, 2t16B +18.86%, 1t4KiB +10.31%, 2t4KiB +12.24%. Separate 8-round syscall tail-latency experiment median p99 improves ~9–13%, BUT rare 2t4KiB ON maximum observed 1.35ms vs OFF 0.156ms so tail latency still unresolved. Code+data ELF difference versus G626 317477B combined (includes DEBUG_FS core), major resident sections approximated +299.71KiB; compressed zImage +84856B (BOOT size, not extra permanent RAM). Sampled `nr_free_rcu` max 636KiB ordinary, 764KiB with 96MiB resident anonymous pressure, 728KiB with 192MiB pressure; additional 43-sample zone test max 209 pages=836KiB in Normal and 0 in HighMem. Pending queue drained after tests; no kswapd/direct reclaim or swap happened: these are moderate pressure tests, **not worst-case memory safety proof**. ext4 concurrent read/truncate+extend, mmap shared writable read, tmpfs and FUSE short smoke all PASS 0 bad bytes; 16MiB sequential cache eviction fadvise test PASS, no RCU marking as expected given narrow Hot4K scope. Host static native + ARM32 builds PASS. Full reproducible logs, Python statistical analyzers, C and shell source at `linux-7.3-port/G632_ARM32_RCU_FULL_PERFORMANCE_MEMORY_QUALIFICATION_20261020.md`, port ledger commits 3439b64 and 5d62a46, tag smt311-g632-physical-rcu-performance-memory-qualification.

**ENGINEERING DECISION: DO NOT ENABLE permanent RCU yet**. Synthetic speedups are reproducible and RAM overhead seems modest on this workload, but it is still a prototype, without actual HighMem pending frees, memory-reclaim/pressure/OOM, LKMM ARMv7 lifetime correctness, KASAN/KCSAN race proof, long-run 24h real Android/UI/phone/GNSS/MTP host and battery tests. Preserve G631 default OFF while investigating. No audio playback/calls/camera shots, no flash/backups in G632 and no GitHub push (owner publishes).

---

# G626 — 20 October 2026: physical MODEM USB PM-hold diagnostic kernel A/B, NOT Linus/Kiryl RCU

G626 physically flashed signed BOOT-only experimental kernel sourced from separate `linux-7.3-port/experimental/G626_xmm_usb_pm_hold_ab` branch Git 4432bceab, one change `pm_runtime_allow(&udev->dev)` -> `pm_runtime_forbid(&udev->dev)` ONLY in Exynos XMM6262 USB child late PM activation. Original G597 source base 08e4784ad, SAME ramdisk/DTB and UTS 7.3.0-pjarczak, NO Linus/Kiryl pagecache RFC. Signed forward ZIP SHA a67fe6746139b84a0a4147b7e4078a47b969796972d53d4a477cdb91949ce48d; TWRP 3.1.1 verified BOOT G597, recovery, G621 GPS and G623 modem/audio guards, install RC0; physical BOOT after write SHA 60ace21324a87df25e0e2adbf486857faa3b38497f7467cbcfdd82274464c1f9, original recovery unchanged. Android boot_completed1, Enforcing, CPBOOT/RIL/audio/GNSS running, USB MTP applied/configured true. Existing G623 vendor and G621 GNSS unchanged. G626 PM hold can increase idle power; no physically verified phone call hangup A/B YET; owner must make two calls consecutively ideally three pairs, capture ACK sequence/time. Signed host rollback to G597 SHA 526fc047d838410a5cece25faa03d9e62ba5cd0bdecbab2cb12a1c13629ff8d2 staged at /data/local/tmp/G626-RETURN-G597-BOOT-TWRP-SIGNED.zip, not installed; no new backup created. 12 hot pagecache pread samples and three 8s concurrent rename integrity stress PASS (2,030,762 reads, 2320 replacements, errors=0) on G626. Observed 24–42% more pread/s than prior G597 sequential sample, NOT evidence of causal PM effect or Linus/Kiryl RFC: no RCU patch installed, thermal/DVFS uncontrolled. Kernel report linux-7.3-port/G626_XMM6262_USB_PM_HOLD_PHYSICAL_AB_20261020.md, ledger Git 13699fd. Latest Oct19 Kiryl 18 RFC patchset NOT IMPORTED into offline Debian build host; exact-source 18/18 fail-closed mbox gate source/test in ledger. NEVER substitute old Oct13 G575.

---

# G623 — 20 October 2026: guarded audio/RIL + modem watchdog physically deployed

Four Android vendor/bin modules G612 audio bridge, G616 all-rmnet probe, G619 modem watchdog and new G619 CBD pidfd helper are now **PHYSICALLY INSTALLED**, no longer merely staged! Signed TWRP four-file ZIP SHA256 20767031d2b053fdd92d94737b460dbc3ecfafd6e88f3b25d51a852291d6972b, builder tools/g623_build_guarded_radio_userspace_ota.py Git 9474191. Exact old hashes, BOOT G597 p9 hash, RECOVERY original TWRP p10 hash, p20 geometry, GPS G621 SHA all preflight PASS in original TWRP 3.1.1-0, install RC0. Verified after write from real SYSTEM read-only four new SHA: audio afcfb90944f53746c05e3bd033d5421782a0428eccf473b8060fe133603f8e65; rmnet probe 8b7f15f0f58ef225c56a39dccec9dea232d6cb3937eaea818e56aafe058fd330; watchdog f8b203ff22d101894e7ffa7c0c48cd4d7ca93c9b2bdf3a71c5926a35adfff86c; pidfd e9b08ba4055d8ee3f988d80bc3bfb8df7b0af204ec4214102704c5f5178ef048, all labels and mode 0755 correct. BOOT and REC unchanged. Android reboot, USB ADB briefly re-enumerated but returned, sys.boot_completed1, SELinux Enforcing, CPBOOT/RIL/audio bridge/GNSS running, network UMTS IN_SERVICE, MTP applied/configured true, GPS G621 unchanged. tools/g623_postinstall_readonly_acceptance.sh run on real SM-T311 PASS RC0; temporary script removed. None of this confirms 60-second HANGUP fixed: real G622 OEM IPC response delayed until 60s remains unresolved. G624 baseline tested running same unchanged G597 kernel, 12 hot pread benchmark and 3 atomic rename concurrency stress PASS, without latest Kiryl RFC installed. Full report linux-7.3-port/G624_PAGECACHE_BASELINE_AND_G623_PHYSICAL_ACCEPTANCE_20261020.md. No backups, modem NV/EFS, calls or GitHub push. Older G612/G616/G619 handoffs saying STAGED are now OBSOLETE. Kernel latest Oct19 RFC uninstalled pending complete source integrity and ARM32 correctness stress; never replace with older Oct13 G575.

---

# G621 - 20 October 2026: G620 GNSS NMEA checksum HAL PHYSICALLY INSTALLED VIA TWRP

Source Git eebfcc7 provides tools/g621_build_guarded_gps_only_ota.py, signed GPS-only ZIP which checks physical BOOT G597, original RECOVERY TWRP 3.1.1 and old GPS HAL checksum before writing EXACTLY one file /system/system/vendor/lib/hw/gps.default.so; separate Git 567b344 adds read-only physical postinstall acceptance. TWRP initially took >35 seconds to appear on ADB but returned as 3.1.1-0; physical p9/p10 and p20 geometry+old GPS hash exactly matched. `twrp install /tmp/G621-GPS-ONLY-SIGNED.zip` SUCCESS RC0, on-recovery readback showed GPS SHA256 102b7ea8f6536baa6a7db7fc8a946f5d29f49e387519bc7d8580489a2f671df2, 0644 and u:object_r:vendor_file:s0. Both p9 BOOT and p10 RECOVERY hashes unchanged after write. Reboot to Android SUCCESS, `sys.boot_completed=1`, kernel `7.3.0-pjarczak`, Enforcing, GNSS/RIL/CPBOOT/audio services all running, USB MTP configured/applied. tools/g621_postinstall_readonly_acceptance.sh executed on physical tablet RC0, G621_GPS_ONLY_PHYSICAL_POSTINSTALL_ACCEPTANCE_PASS. G620 new GPS HAL **IS NOW INSTALLED IN PRODUCTION**, unlike old G620 handoff. No backup, audio playback, calls, camera, receiver TTFF or satellite outdoor tests, modem/CP or kernel changes. Full report linux-7.3-port/G621_GPS_NMEA_SINGLE_FILE_TWRP_PHYSICAL_DEPLOYMENT_20261020.md. ZIP SHA256 e6adff00204333e5b2f53db27bd23e801fe6e52666b87f17d175f7ac06c083b1, 1,618,125 B; generated release artifact is NOT a source commit. Other staged candidates remain NOT-INSTALLED.

---

# G620 - 20 October 2026: BCM4752 GNSS NMEA counted-frame suffix hardening

Android source Git 1212425 corrects gps-v195/gps_proxy.c v308_nmea_checksum(), previously accepting valid XOR sentences with garbage after CR/LF or embedded NUL in a counted NMEA payload. It now requires exactly *HH, *HH\r, *HH\n or *HH\r\n with no other bytes, without out-of-bounds reads for truncated checksum. Generator tools/test_g620_nmea_frame_integrity.py extracts actual C parser into 530-case strict framing test: Debian GCC 530/530 PASS and ASan+UBSan 530/530 PASS. G608 GNSS/SUPL host regression gate PASS. Android ARM32 gps.default.lt013g.v195 build PASS 8/8, autogenerated Soong lt013g-g620-nmea-frame-test build PASS 10/10. Physical SM-T311 no-modem/no-receiver test executed as shell 530/530 PASS, test deleted afterward. Candidate GPS HAL SHA256 102b7ea8f6536baa6a7db7fc8a946f5d29f49e387519bc7d8580489a2f671df2 staged ONLY at /data/local/tmp/G620-gps-default-NOT-INSTALLED.so, installed /vendor/lib/hw/gps.default.so remains previous SHA 2e326e700513cb86b79d6cd7ef77adb3d1b595c46e8b69916ea08ae5c7485cd9. No production installation, GPS session, TTFF or actual satellite fix test. Android SYSTEM G591/kernel G597 unaffected, no backup, reboot, audio, camera or calls. Report linux-7.3-port/G620_BCM4752_GNSS_NMEA_STRICT_FRAME_INTEGRITY_20261020.md.

---

# G619 - 2026-10-20: all three exceptional CBD signals use exact-process pidfd

Android source commits a6e19ea and 69de516 add a fail-closed native `t311-cbd-pidfd-signal` helper. It opens pidfd FIRST, verifies expected t311-cbd path and /proc field22 starttime, then signals via pidfd_send_signal; the harmless --check mode uses signal 0. Watchdog's THREE exceptional CBD SIGKILL paths (missing HAL, empty SIM, offline) all call this helper, with no fallback to `kill -9 $pid`. Soong Android11 ARM32 combined build PASS 280/280. Host native test and ASan+UBSan PASS; isolated native ARM32 test on real SM-T311 PASS, only its own forked dummy process signaled, NEVER modem. vendor_file_contexts build PASS using existing watchdog probe SELinux label from lt01-common Git da0dc46. Source gate tools/g619_cbd_pidfd_source_gate.py PASS, prior G614-618 mock tests PASS. Helper ELF SHA256 e9b08ba4055d8ee3f988d80bc3bfb8df7b0af204ec4214102704c5f5178ef048 staged at /data/local/tmp/G619-t311-cbd-pidfd-signal-NOT-INSTALLED. New watchdog SHA256 f8b203ff22d101894e7ffa7c0c48cd4d7ca93c9b2bdf3a71c5926a35adfff86c staged at /data/local/tmp/G619-t311-modem-watchdog-NOT-INSTALLED. Golden vendor watchdog untouched, and no helper installed in vendor. No real CBD termination or SELinux production-domain validation; telephone HANGUP fix NOT proven. No backups, reboot, flash, sound, camera or phone calls. Full report linux-7.3-port/G619_CBD_PIDFD_ATOMIC_SIGNAL_SOURCE_ANDROID_20261020.md.

---

# G618 — 20 October 2026: bounded mobile-data probe and CBD PID reuse safety

G617 Android source commit 98857a3 protects the native t311-mobile-session-probe call in modem watchdog with `timeout -k 2 3`. Timeout, failed network inventory, unavailable executable or active data all block modem restart. Source-extracted mocked tests 12/12 PASS on Debian and SM-T311; real Android timeout harmlessly stopped a `sleep` command with RC124. G618 source commit e4e791d adds `cbd_pid_identity()`; before both exceptional CBD SIGKILL branches it compares the exact executable path and `/proc/<pid>/stat` field22 process birth time before and after final safety checks. Synthetic tests 14/14 PASS on Debian and physical Android via shell without sending signals. This mitigates PID reuse, though it cannot make shell signals fully atomic. Previous watchdog tests G614 12/12 and G615 49+7 PASS; Android `m t311-modem-watchdog` PASS. Latest candidate SHA256 bc3ccd109fe295d9cc960544d32c3783559da8f79604e08ed6183cbbf3f01f21 staged at `/data/local/tmp/G618-t311-modem-watchdog-NOT-INSTALLED`. Installed vendor watchdog remains SHA dfab4aeb0f2c59b904bff9fcdc988e464f2e911021864f12475a597ca4c159c6 unchanged. No actual CBD restart/kill, SIM/PIN, calls, backup, flash, reboot, sound or camera. Report `linux-7.3-port/G617_G618_MODEM_WATCHDOG_BOUNDED_NETLINK_AND_PID_IDENTITY_20261020.md`. Owner-present phone acceptance still OPEN.

---

# G616 — 20 October 2026: any rmnet data session blocks modem watchdog recovery

Source Git b11d170 extends radio/t311_mobile_session_probe.c beyond rmnet1: it now checks all numeric rmnet interfaces (e.g. rmnet0/rmnet1/rmnet2), treating assigned IPv4 or non-link-local/global IPv6 as ACTIVE. rmnet1 still must be present for a proven inactive result, and any getifaddrs error fails closed. Host C test 17/17 PASS, ASan+UBSan 17/17 PASS. Android ARM32 Soong t311-mobile-session-probe and independent t311-mobile-session-probe-test compile PASS; native test executed on real SM-T311 17/17 PASS and was deleted. G616 test SHA256 ccc02bc87993ce8a530d9a63761fad51c3c244b008d2e454d45cffcc831f61f8. Candidate probe 8b7f15f0f58ef225c56a39dccec9dea232d6cb3937eaea818e56aafe058fd330 and G615-rebased watchdog comment variant 9437976ab4520c447140f4e247778080817f2586f4a3e8911f79be2f786d4948 are staged at /data/local/tmp/G616-...-NOT-INSTALLED, neither is deployed. Installed golden vendor watchdog and probe remain unmodified. The real tablet has rmnet0/rmnet1/rmnet2; rmnet1 currently had an assigned cellular IPv4. No modem restart, calls, backup, flash, radio PIN/PUK, sound or camera activity. Full report linux-7.3-port/G616_MULTI_RMNET_DATA_SESSION_FAIL_CLOSED_20261020.md.

---

# G615 - 20 October 2026: final CP restart state guard native tested, NOT installed

Android source commit d308799 adds final_recovery_guard checks just before all three ctl.restart paths: missing HAL requires modem healthy/HAL missing, empty SIM requires modem healthy/strict positive native HAL probe (timeouts NOT success), offline requires modem still unhealthy. Every path also needs no_active_modem_session; stale HAL/USB snapshots can no longer authorize a restart. Existing atomic one-restart-per-boot policy unchanged. tools/g615_modem_watchdog_final_guard_test.py extracts actual source and mocks all external actions, host and physical Android /system/bin/sh 49/49 state cases + 7/7 strict probe cases PASS; G614 12 state-reader tests remain PASS. Android m t311-modem-watchdog build 3/3 PASS. Candidate 17040B SHA256 615005d6727c46b7074fe685e992be16a51a37f88dff52e47316d2f6b75be521 staged as /data/local/tmp/G615-t311-modem-watchdog-NOT-INSTALLED, not run as watchdog and not installed to vendor. Golden /vendor/bin/t311-modem-watchdog SHA dfab4aeb0f2c59b904bff9fcdc988e464f2e911021864f12475a597ca4c159c6 retained. CPBOOT/RIL/audio running. No reboot, actual modem restart, call, backup, flash, sound, camera, or SIM PIN/PUK operations. Full report linux-7.3-port/G615_FINAL_MODEM_RESTART_GUARD_HAL_CONFIRMED_20261020.md. Supervised call/HANGUP acceptance remains open.

---

# G614 - 20 October 2026: modem watchdog blocks restarts on failed state reads

Source Git 9157dd6 tightens no_active_modem_session(): airplane-mode SettingsProvider read and gsm.sim.state getprop read must each exit successfully before comparison. Any failed read, active data session, nonzero airplane mode or known SIM state blocks the auto CBD restart. Host tool tools/g614_modem_watchdog_guard_test.py extracts the real source function and mocks all external functions; 12 cases passed on Debian and 12/12 on the physical SM-T311 under Android /system/bin/sh, with zero modem operations. Packaged t311-modem-watchdog target build PASS. New script SHA256 aea410d95d1395d87fa56c868095a1b6d9b6fc545c031188d0b2ddd76f1ed964 staged under /data/local/tmp/G614-t311-modem-watchdog-NOT-INSTALLED, never executed or installed. Existing vendor watchdog SHA256 dfab4aeb0f2c59b904bff9fcdc988e464f2e911021864f12475a597ca4c159c6 remains in service. Android G591/kernel G597 production untouched; no reboot, backup, flash, calls, PIN/PUK, CP writes, audio or camera tests. Report: linux-7.3-port/G614_MODEM_WATCHDOG_FAIL_CLOSED_STATE_READ_20261020.md.

---

# G613 - 20 October 2026: ARM32 socket-only audio bridge regression physically PASS

Source `device/samsung/lt013g` Git 282241a adds an explicit Soong `cc_test` module `t311-audio-ril-io-test` and corrects `NDEBUG` so ARM32 builds actually execute each assertion. Target `m t311-audio-ril-io-test` PASS 8/8. Physical SM-T311 test binary SHA256 333fd7dbe2f3a0c01989a2d7492394ff4b2af506b36e97c5afcac19544514243 ran as plain ADB shell: 5/5 socket cases PASS, exit0, then temporary test removed. Nothing touched audio HAL, rild, CP, SIM, or calls. G612 candidate bridge binary remains staged NOT-INSTALLED with SHA afcfb90944f53746c05e3bd033d5421782a0428eccf473b8060fe133603f8e65. Installed golden bridge remains SHA79faf768b1af78cca260018988950e2de8f8ec591e9022f5b5f47ae159cb4041. Previous 60s HANGUP/second-call audioserver deadlock is not fixed/accepted until owner-led test. Full report `linux-7.3-port/G613_AUDIO_RIL_NATIVE_SOCKET_ARM32_PASS_20261020.md`. No backup, reboot, OTA, camera, sound or calls.

---

# G612 - 20 October 2026: audio RIL socket deadline candidate, not installed

Android device source commit aaa0233 adds radio/t311_audio_ril_io.h with CLOCK_MONOTONIC 500ms absolute deadlines across partial Unix-socket reads/writes; both RIL frame header and body share one deadline. No OEM RIL timeout or audio API changed. Host 5 socket regression cases PASS uninstrumented and again with ASan+UBSan. Android11 ARM32 m t311-audio-ril PASS 6/6. Candidate SHA256 afcfb90944f53746c05e3bd033d5421782a0428eccf473b8060fe133603f8e65 was pushed to /data/local/tmp/G612-t311-audio-ril-NOT-INSTALLED and verified on device, **NOT EXECUTED OR INSTALLED**. Existing golden bridge /vendor/bin/t311-audio-ril SHA256 79faf768b1af78cca260018988950e2de8f8ec591e9022f5b5f47ae159cb4041 remains active; RIL/CP services continue running. This change has NOT passed owner-led HANGUP, two-call and audioserver regression tests and cannot be called a phone fix. No backup, flash, reboot, sound, camera, call tests. Details /home/debian/Projekty/samsung/linux-7.3-port/G612_AUDIO_RIL_SOCKET_WHOLE_FRAME_DEADLINES_20261020.md.

---

# G610 - 20 October 2026: OTG compiler acceptance ONLY; physical HOST disabled

G609 hardware/interfaces commits 6aa103676 + corrective 6a3954e8d (net source identical to previously tested G603): prevent conflating MAX77693 own BOOST-supplied VBUS with external charging input. Active HOST continues to require EXTCON_HOST, EXTCON_USB=0, valid PHY/HCD and verified physical BOOST; pre-arm requires known zero VBUS. Native test built from production expression PASS 32 combinations, prior G603 kernel/HAL static schema PASS, ARM32 usb gadget HAL build PASS 7/7. New binary SHA256 2540a4a1840a0814e79d578982e8825080ff03d811b84584c74ff88b5963f403 matches previous G603 staging. Erroneous provisional G609 staging removed; active system HAL untouched.

G610 separate Linux 7.3 build directory compiled EXPERIMENTAL CHARGER_MAX77693_T311_OTG_EXPERIMENTAL=y: full vmlinux/zImage and Exynos4212 DTB PASS, isolated configuration differs only by that flag, DTB byte-identical to production G597. This is NOT physical OTG support: the real DTS already contains t311,otg-boost-experimental, therefore flashing experimental kernel would actually expose a potential 5V BOOST path. NEVER deploy without measured VBUS/current limits, charging exclusion, adapter detach and ADB rescue under owner supervision. Host-only experimental zImage is NOT a release artifact and was not pushed or flashed. Physical Android G591 + kernel G597 unchanged, SELinux Enforcing, no audio/camera/call testing, no backup/reboot. Details: /home/debian/Projekty/samsung/linux-7.3-port/G609_OTG_VBUS_OWNERSHIP_AND_RELEASE_SAFETY_20261020.md and G610_OTG_EXPERIMENTAL_KERNEL_COMPILE_ONLY_20261020.md.

---

# G603 - 2026-10-20: OTG HAL source hardened, BOOT/ROM unchanged

Android hardware/interfaces commit c38776955 corrects OTG sysfs consistency (one record per interlock check) and releases HAL-owned EHCI runtime-PM override on safe service shutdown. Android ARM32 USB gadget service build PASS 6/6; `linux-7.3-port/tools/g603_verify_otg_interlock.py` source/schema safety checks PASS. Built binary 34828B SHA256 2540a4a1840a0814e79d578982e8825080ff03d811b84584c74ff88b5963f403 staged at `/data/local/tmp/g603-usb-gadget-service-NOT-INSTALLED`, NOT executed or installed. Kernel G597 OTG experimental config disabled, four t311_* sysfs control endpoints absent on device: actual OTG pendrive use is UNVERIFIED and BOOST MUST NOT be armed during USB-PC debugging. Current Android G591/kernel 7.3.0-pjarczak G597, Enforcing, system USB HAL running, USB ADB working. No backup, flash, reboot, sound, camera, calls or GNSS tests. Full report: `linux-7.3-port/G603_OTG_INTERLOCK_SINGLE_SNAPSHOT_AND_EHCI_SHUTDOWN_20261020.md`.

---

# G602 - 20 October 2026: FMQ ARM32 regression PASS; source continuity

Physical Android G591 + kernel G597 remains installed. G601: `system/libfmq` Git 41568a4 tests a descriptor without EventFlag grantor: ARM32 `mq_test` build PASS and actual tablet one focused test PASS (1/1, 1ms); no changes to installed ROM. `system/libhidl` Git 5f5fa78 requires retry=true for an extra legacy missing-HAL 1-second wait; ARM32 build PASS, not runtime validated/deployed. Full 64-bit Linux host `libhidlbase` build blocked by preexisting Binder ABI type mismatch; no source hack left in libhwbinder. G602: tracked commit inventory of 866 repos plus read-only scan of nonignored untracked files - 7 repos / 364 files / 12 source-like, all in historical lt01-common paths. This does not cover ignored files and is not a license verdict. No new backup, flash, reboot, sound or camera tests. Full records: `linux-7.3-port/G601_FMQ_HIDL_RUNTIME_REGRESSION_ARM32_20261020.md` and `linux-7.3-port/G602_UNTRACKED_SOURCE_AUDIT_20261020.md`. Host Dolphin MTP and approved secure Wi-Fi ADB remain OPEN.

---

# G598 - 20 October 2026: silent live USB/PMIC/memory audit, NO FLASH

G598 on-device script `tools/g598-silent-peripherals/readonly_g598.sh` executed via normal ADB shell and SHA256 independently matched Debian. Android G591/BOOT G597 boots and preserves USB MTP configured/applied, Enforcing, cpboot/rild/GNSS and zRAM768MiB. Restricted kernel log window: OPP rejects 0, devfreq-event failures 0, DWC2 endpoint-stop warnings 2, kernel fatal 0, crash fatal-signal 0. The DWC2 events have not been proven to corrupt MTP data. CPU schedutil active, no swap in/out yet. User-level sysfs reads of GPU/Exynos devfreq and zRAM algorithm were denied, so they are UNAVAILABLE, NOT hardware PASS/FAIL. Host-side Dolphin MTP copy remains UNVERIFIED because the workspace has no host USB/GVFS device access. Encrypted Wi-Fi ADB still requires native user network approval, switch 0, trusted count 0. No sound, camera, SIM, call, location, backup, reboot, or BOOT/SYSTEM update. Full report: `linux-7.3-port/G598_SILENT_USB_PMIC_DEVFREQ_SNAPSHOT_20261020.md`.

---

# G597 - 20 October 2026: LIVE Android G591 + Linux 7.3.0-pjarczak

Installed G597 BOOT p9 SHA256 7022bffae070737ff3eec9da218f754d4d569f9b14821ce8fb3ebfa967dc1e28. Original TWRP p10 unchanged. Source kernel 08e4784ad fixes error propagation of Exynos devfreq-event sample after successful counter rearm. Signed TWRP BOOT-only OTA source commits 1e5a25a and 6cf4c69, ARM32 build PASS, guards PRE/POST and physical p9 SHA PASS. The intermediate G596 build with "+" version suffix has been replaced; uname is exactly 7.3.0-pjarczak. Android11 G591 boots (boot_completed=1), Enforcing, modem CP/RIL/GNSS services running, USB MTP applied/configured, zRAM 768 MiB. No backups, audio/camera tests, calls, PIN/PUK or data/EFS/CP writes. MTP PC read/write, encrypted ADB WiFi owner network approval, two-call modem/HANGUP, PMIC/GPU, OTG and outdoor GNSS still need acceptance. This is not evidence of improved performance or recovery from physically injected devfreq errors. Complete report: /home/debian/Projekty/samsung/linux-7.3-port/G597_EXYNOS_DEVFREQ_ERROR_AND_EXACT_RELEASE_PHYSICAL_20261020.md.

---

# G594 — 20 October 2026: current LIVE tablet, silent tests only

SYSTEM Android11 G591 was installed in TWRP with full SYSTEM SHA
229e1a8cbf037fe68cfa8d190771f13c0207b46c0c31f874d0af656ebbf31ab1,
BOOT G582 Linux7.3.0-pjarczak previously installed. Tablet currently boots
normally: sys.boot_completed=1, SYSTEM/VENDOR fingerprints both SMT311G591,
Enforcing, 768MiB zRAM, 2-core CPU governor schedutil, Mesa26.2.4.
G594 source-only silent health audit commits 014e717,24ecf3a,d19bf0e
ran on physical tablet without root; no further camera/audio tests.
Own temporary Camera2 probe removed. No GNSS foreground probe present.

Current verified: CP daemon and RIL running, ril.cbd.boot_done=1,
SIM state LOADED, GPS daemon running, USB Gadget MTP applied/configured,
USB ADB running, zero OPP_REJECT/fatal kernel/Android crash markers in
sampled logs, two DWC2 stop-endpoint timeouts during successful reattach.
PC-side Dolphin MTP actual file transfer NOT independently verified after
G591; do not assert that from Android USB gadget state.

Secure encrypted Wi-Fi ADB port5555 still OFF: Android requires standard
consent for this Wi-Fi network, 0 trusted AP entries. Do not bypass via
injecting BSSID/keys or falling back to insecure adb tcpip.
CPU cpufreq-dt/schedutil **IS scaling**: in 8 seconds stats total_trans
increased 174, 200MHz residency +234 ticks, 1400MHz +504 ticks; a
single scaling_cur_freq=1400MHz sample is not evidence of stuck DVFS.
No voltage or OPP values changed. Zero new backups/reboots/flashes
in G594. User explicitly forbids noise, audio and camera diagnostics.
Oct19 Kiryl refcount-free page-cache RFC unflashed/unverified, no A/B.

---

# G589 ACTUAL PHYSICAL RELEASE (2026-10-20) — ANDROID G586 + KERNEL G582 BOTH INSTALLED

Older G586/G587/G588 historical sections below must NOT be interpreted
as present state. Samsung SM-T311 now runs the G586 Android11 SYSTEM
installed through signed G587 TWRP OTA, exact p20 raw SHA256
2b7f9571b14f3a85e325fd0ef7f9cf1c9fdd56f401444210dbc8ae2dd1a6bd09;
ro.system and ro.vendor fingerprints both SMT311G586.
**G582 Linux7.3 kernel also physically installed** through BOOT-only
signed G589 TWRP OTA built by tracked source
tools/g589_build_guarded_boot_ota.py (Git2faf827). p9 BOOT physical SHA
f7c5091dcba5cbbf012d9eab5ac4a505834a813e272442d8ad8c00c24ae6ee65,
p20 SYSTEM unchanged, p10 original TWRP unchanged. TWRP pre/post
checks and AOSP updater RC0. No new backup, no data wipe or EFS/CP/SIM writes.

After G589 flash: sys.boot_completed=1, bootanim stopped, kernel
7.3.0-pjarczak #18 Oct20, Enforcing, 768MiB zRAM, Mesa26.2.4 Mali400,
RIL/audio/GPS services running. USB Gadget HAL MTP current/applied/configured,
screen-unlocked MTP preference persists through reboot; ADB via USB.
Desktop Dolphin physical file read/write not yet retested after new SYSTEM.

G589 physical three-run 2-second warm-page pread ARM32 benchmark medians:
1t16B 736407/s, 2t16B 1421376/s, 1t4096B 454305/s,
2t4096B 874207/s. Historical G551 single measurements are lower,
but newer ROM, kernel and thermal conditions differ: this is NOT a
controlled A/B and does NOT prove performance caused by Exynos-PMU,
XArray, devfreq diagnosis or the unavailable Kiryl Oct19 RFC.
**Experimental RCU-free pagecache is NOT installed.**

OPEN: native encrypted TLS Wi-Fi ADB 5555 requires explicit Android
trust dialog for currently connected AP; trusted WiFi network count0,
do not bypass via plaintext or userless keystore injection. MTP actual
Dolphin copy, 2nd modem call and hangup UI, GPS weak indoor C/N0
(no fix indoors expected), real camera/video/sensor/suspend/OTG,
PMIC -EIO cause and black sliding screen stability not yet fully
accepted. Source code/guards committed for user-run public GitHub push.
No proprietary OEM binaries or personal device logs should be published.

---

# G586 ACTUAL RELEASE STATE — 20 October 2026

Samsung SM-T311 physically runs OLD Android11 SYSTEM + Linux7.3.0-pjarczak;
boot_completed1, SELinux Enforcing, Mesa26.2.4, USB ADB, rild and GNSS HAL
running, zRAM 768MiB. Android startup fingerprint warning still present:
ro.system ends debian10190306 vs ro.vendor debian10190307.

G586 FULL NEW SOURCE Android ZIP SHA256
5ac8e055d2ffa326a5b72e9590db6fbb556c3c61668fbd84ed174c56d6f00a34,
raw ext4 SYSTEM SHA256
2b7f9571b14f3a85e325fd0ef7f9cf1c9fdd56f401444210dbc8ae2dd1a6bd09
size 2235564032B; real e2fsck -fn exit0, extracted ext4 gps.default.so
SHA2e326e700513cb86b79d6cd7ef77adb3d1b595c46e8b69916ea08ae5c7485cd9
matches compiled G586 GNSS HAL, temporary GNSS test app NOT in ZIP/ext4.
Verified system/vendor fingerprints EXACT equality SMT311G586, 18 SELinux
labels, native encrypted ADB WiFi5555/USB replug gate, golden BOOT,
RECOVERY, OEM RIL/audio and Mesa library parity (HOST ONLY).
New narrow zero-legacy-SV-vs-fresh-GPGSV source fix Git5dcb2a5
tested10 GCC ASan/UBSan cases, real Android11 ARM32 build PASS. 11/11
G586 USB data slices already physically sent and remote SHA verified at
/data/local/tmp/SM-T311-G586-TRANSPORT64, 571409322 compressed bytes.
After proof, only old 11 G573 temporary compressed slices deleted on tablet,
all G573 sources/ZIP on Debian intact, device /data free1462552KiB.

**SYSTEM G586 NOT FLASHED**: previous raw p20 write blocked by tool
safeguard, not circumvented; do not mark popup, WiFi TLS, cable replug,
GNSS de-flicker as PHYSICAL PASS. G582 BOOT similarly staged as data
ONLY, not flashed. Tablet sits on desk; lack of GNSS fix indoors
is not a failure. True G585 GNSS RF max3 positive C/N0 of max12 entries,
max11dBHz, no real fix; do not invent satellites. Latest Kiryl Oct19
folio RCU no-ref RFC source still unavailable due lore HTTP403, no A/B.
All improvements stay in committed source for user's future GitHub push,
not in a misleading physically certified release. Do not commit OEM blobs.

---

# G585 PHYSICAL GNSS SIGNAL CORRECTION 2026-10-20

On live SM-T311 Linux7.3 Android11, full 70-second second indoor desk probe
with updated source-built signed G585 APK PASS: 90 GNSS status callbacks,
maximum 12 satellite LIST ENTRIES but max3 with positive C/N0; max11 dB-Hz,
0 strong >=25dB-Hz, 0 position fixes (indoors, FIX NOT REQUIRED).
The old 11/12 visible slot counts are not 11/12 received strong RF signals.
G583 earlier 70s run had115 callbacks max11 entries max10 C/N0.
Oct16 G385 (earlier OS/kernel/date) had71 callbacks max7 entries max32 C/N0
and max2 strong >=25: significant observed difference, causal regression
UNPROVEN due weather/time/orientation/assistance/driver differences.
GNSS returns mStarted=false after probe, test APK removed, USB ADB still up.

G585 source commits: 17585eb adds positive-C/N0 counting, G585 ARM32
full APK build PASS, e9b648d repairs G323 direct GNSS NMEA extraction unit
test after telemetry counters expanded (4/4 callback fixtures), 60ec472
records privacy-sanitized real G585 measurement. No raw NMEA, PRN, coordinates
or SIM identifiers stored. GNSS services run but RF is weak in apartment;
verify native/NMEA callback source and AGPS, do not demand indoor fix.

G573 SYSTEM remains NOT INSTALLED due tooling safeguard blocking native
p20 write in real TWRP. Old physical system fingerprint mismatch popup remains.
G582 kernel zImage/BOOT host built and BOOT file SHA staged on tablet /data
only, NOT flashed; PMIC VDD_MIF/INT -EIO root cause OPEN.
Source Git kernel eb42034fe, stock PMIC guard source b667ec6,
project G584 matrix Git593582a, device G584 public matrix Git d088650.

---

# G584 — aktualny fizyczny stan SM-T311, 2026-10-20

Ta tabela supersedes historyczne nagłówki G379/G439/G552 poniżej.
**Nie** uznawaj starszych wersji ani starego SwapTotal=0 za aktualny stan.
Tablet nadal uruchamia Android11 ze starym SYSTEM i Linux 7.3.0-pjarczak
G551; nowe G573 SYSTEM i G582 BOOT nie zostały wgrane.

| Podsystem | Stan źródeł / kompilacji | Fizyczny wynik 2026-10-20 | Następny warunek |
|---|---|---|---|
| Android / start | G573 budowa + SHA PASS, fingerprinty wyrównane w obrazie | Stary system: ro.system ...debian10190306 różni się od ro.vendor ...debian10190307, okno błędu nadal jest | Legalny, obsługiwany flash G573 i zimny boot bez ostrzeżenia |
| TWRP / ADB USB | Stary oryginalny TWRP, test IO64 PASS | ADB reboot recovery wszedł do TWRP 3.1.1, p9/p10 i p20 geometria PASS, powrót do Androida PASS | Powtórne boot recovery oraz transfer/naprawa USB |
| Instalator G573 | 11 poszatkowanych plików SHA PASS i 5 pełnych gzip | 11/11 fragmentów G573 rzeczywiście na /data, SHA PASS; polecenie zapisu p20 zablokowane przez zabezpieczenia narzędzia, **SYSTEM NIE ZAPISANY** | Obsługiwana instalacja, kompletny SHA p20, boot |
| Kernel / Android11 | v7.3 final + 29 porting commits, G582 boot SHA f7c5091d... host PASS | Obecny kernel 7.3.0-pjarczak, Enforcing, boot_completed1; G582 **niewgrany** | Osobny test G582, nie mieszać z RCU eksperymentem |
| RAM | zRAM 768MiB LZ4 / MGLRU | SwapTotal 786428 kB, MemAvailable ok.864000 kB; wcześniejszy G552 SwapTotal=0 już nieaktualny | Testy obciążeniowe, nie zwiększać swap w ciemno |
| GPU / LCD | Mesa 26.2.4 HWC FIMD/Lima | GLES Mali400 26.2.4, HWC missed frames 50, wcześniejszy przesuwający czarny obraz po resecie ustąpił | Grafika i devfreq podczas długiego testu + zasilanie |
| PMIC / zasilanie | G582 diagnostyka -EIO git eb42034fe host ARM32 PASS | KMSG poprzedniego startu powtarzał błędy VDD_MIF/VDD_INT -5; root cause NIE ZNALEZIONY. G584 test dowodzi stock op_mode3 BUCK1/3 | Zestawienie chwili błędu z clock/PMIC, bez zmiany napięć |
| MTP / USB | G573 źródłowo ma nowe reattach | Użytkownik wcześniej potwierdził MTP na Debianie; dziś USB ADB działa | Fizyczne replug i transfer pliku po G573 |
| ADB Wi-Fi | Nowy G573: TLS 5555 + fallback i trusted AP | Stary system native adb_wifi_enabled=0 po reboocie; nie włączamy plaintext ADB | Szyfrowane parowanie i port 5555 po G573 |
| GNSS BCM4752 (tablet na biurku) | G583 test app i status code w Git b257e37 | 70s: HAL START PASS, 115 status event, max11 wpisów SV, max C/N0 tylko10 dB-Hz, 0 >=25, fix0, powrót do idle PASS | Audyt GSV i natywnych statusów; brak fixa indoors NIE jest błędem |
| GNSS wcześniejszy comparator | G385 2026-10-16 | 70s max7 SV, max C/N0 32dB-Hz, 2 >=25dB-Hz, 0 fix | Porównanie RF bez twierdzenia że nowy kernel jest przyczyną |
| Audio / modem | złoty HIDL V191; kod radio źródłowo istnieje | rild oraz audioserver running, dzwonek wcześniej potwierdzony | Owner-led 2 rozmowy, 60s HANGUP OEM203 i audio deadlock |
| Kamery / czujniki | HAL źródłowo obecne | 2 aparaty, ALS/proximity/accelerometer/magnetometer zarejestrowane | Realne zdjęcia, video, jasność, near/far, ruch |
| OTG / standby | bezpieczny domyślny BOOST OFF | Brak weryfikacji pendrive OTG, bateria USB 100%, boot/ADB stabilne podczas G583 | Testy fizyczne później, bez PC+OTG jednocześnie |
| Page cache RCU 19 Oct | Oct13 eksperyment G575 kompiluje ARM32, starszy niż pełne RFC Oct19 | Nowego BOOT z page-cache **nigdy nie testowano na tablecie**, brak wyniku OFF/ON | Pozyskanie pełnej serii Oct19, korekty race, oddzielny A/B |

**Zasady:** bez backupów i bez ingerencji w EFS, SIM PIN/PUK, zasilanie OTG
czy płatne połączenia. Każda zmiana w repozytorium Git, test host lub
fizyczny zaznaczony osobno. Proprietary Samsung blobs nie są automatycznie
publicznym kodem do GitHub. Nie modyfikuj GPS tak, by raportował fikcyjne
satelity, sygnały lub pozycje.

---

# Samsung SM-T311 — master hardware / feature matrix
Updated G403 2026-10-17: REAL LIVE G379 Android11 boot1 Enforcing Plus LOADED old GPSD running GPS0 idle. Latest FULL coherent G403 TESTKEY signed 484326152B SHAb6df8dca... physically staged root0600 device whole SHA PASS NOT INSTALLED, only matching real old TWRP readonly3029B SHA6459d29d... physically SHA PASS but unexecuted, matching afterboot-only G403 read-only4213B SHA42496b79... physically SHA PASS. New actual G401 original C local CP CLOSED -EIO leaves session pending with no trigger, G403 exactly 3 capped CLOSE attempts with max2x100ms retry,8 corrected C source scenarios PASS plus G330-G333/G396/G397/G400/G401 regressions. Coherent full ARM32 targetfiles1149695333B SHAbcf43207..., only four permitted changed contents vs G401, nine unsafe mutation checks rejected; original factory BOOT/TWRP/RIL/audio/ART/SELinux and selected /misc unchanged. Independently signed SYSTEM1122914304B byte parity PASS. Owner manual TWRP G403-only guard then separately approved full coherent G403 install, real mobile Network VALIDATED/NTP/SUPL/UTC and stable outdoor GPS3D unverified. Full historical PRIVATE G389 archive tablet intact, newer full G390 private host-only connector safety block.

Status: PASS = grounded tested function; PARTIAL = only enumerated or partly demonstrated; FAIL = reproducible defect; PENDING = missing physical acceptance; BLOCK = release blocker.

Reference ORIGINAL Samsung SM-T311 8in 3G Exynos4212, 1.5GB, never confuse with SM-T310 Wi-Fi or SM-T315 LTE. Samsung original GPL kernel resides in kernel-stock-samsung; original Android 7.1.2 OEM Samsung RIL is privately held and NEVER redistributed. Public hardware reference: https://hirek.prim.hu/download/attach/98029/9/0/574424-1079395189.pdf ; technical runtime PM specification: https://docs.kernel.org/power/runtime_pm.html .

| Component | State | Evidence | Remaining acceptance |
|---|---|---|---|
| Android11 init / UI boot | PASS | Physical boot_completed1; SystemUI live | Full integrated clean install |
| Kernel Linux6.12.112 | PASS | Current G379 boot1 Enforcing, GNSS Binder active, Samsung SEC reboot INFORM3 recovery mapped in source. G389 traced recovery failure to Android11 BCB FSTAB+SELinux label rather than proven kernel reset bug, so no speculative kernel changes. | Long-term kernel reboot/suspend stability after owner physical install G389 and controlled tests |
| SELinux | PARTIAL | Global Enforcing on G379; GPS Binder private mount works, GNSS mounton AVC permissive=1; rild/gps/backuptool/su scoped exceptions remain | Strict vendor domain policy without violating AOSP neverallows |
| Full OTA / ART integrity | PARTIAL | G379 physically installed/boot1. G382 rebuild target-files exit0, integrated audio VINTF+ARM32 corrected libaudiohal and private GNSS binder typed policy, final guarded signed OTA SHA 1c969cae..., 1,122,914,304 SYSTEM bytes audited PASS, staged on tablet with matching SHA | G382 physical TWRP install and Android boot acceptance, remaining SELinux/OEM gates |
| Stock OEM legal redistribution | BLOCK | Proprietary blobs privately extracted | Public licensing and separate legal vendor provisioning |
| LCD FIMD 1280x800 | PASS | Physical wm 800x1280; UI video | Rotation, brightness stress |
| Touch input | PASS | Previous physical test | Multi-touch, edge and gestures |
| Mali GPU / HWC / G2D | PASS | Previous physical graphics regression | Long graphics stress |
| Hardware H264 decode | PASS | Prior HW pipeline accepted | Multi-player codec matrix |
| Camera rear 5MP | PARTIAL | Camera 0 enumerated and older physical tests | All still/video/AF modes |
| Camera front 1.3MP | PARTIAL | Camera 1 enumerated and older tests | All still/video modes |
| Wi-Fi 2.4/5GHz | PASS | Physically connected, data prior validated | 5GHz, roam/tether |
| Bluetooth | PARTIAL | Bluetooth manager state ON | Pair/transfer/headset profiles |
| microUSB/ADB/TWRP | PASS | Live ADB USB, recovery and OTA | OTG USB-host peripherals |
| Internal eMMC/FUSE | PASS | Boot + /data mounted | fs stress/long writes |
| microSD card | PENDING | Stock hardware supported | Physical insertion/read/write/eject |
| Multimedia audio out | PARTIAL | G379 audio VINTF fragment physically inserted, audioserver and HIDL 6.0 registered; media.audio_policy found and Android boot1 | Actual speakers, jack, mic and audio route quality |
| Mic / voice audio | PENDING | No reliable bidirectional call | Recorded input, earpiece, headset routes |
| Incoming calls | FAIL | User ringback but no tablet UI | CP -> OEM events 1018,1001 -> call list -> Telecom |
| Outgoing calls | PARTIAL | RIL DIAL SUCCESS, DIALING and ALERTING | ACTIVE, speech and hangup acceptance |
| SIM detection 2G/3G | PASS | Plus LOADED UMTS | Other compatible SIMs and roaming |
| SIM PIN/PUK guard | PASS | G368 atomic once + invalid reject, zero PIN/PUK seen | User-present valid PIN card only; never brute force |
| Cellular packet data | PARTIAL | G379 Plus SIM LOADED UMTS data state2 rmnet1 /32 off-link gateway and live netd ENETUNREACH101. G387 model/iface/32 gateway host route source717c949 Java10+4 PASS; G389 cumulative signed ROM staged and SHA confirmed, original libsec-ril/ART/GPS CIL unchanged; not installed. | After G389 safe install verify gateway host+default routes, Network VALIDATED, DNS/NTP/SUPL, later owner SMS+voice; no forced APN/data/CP/PIN/PUK operations |
| HSIC hostwake runtime PM | PARTIAL | G370/G371/G372/G373 kernel wake/PM, host tests PASS, cold-start data not accepted | CarrierConfig generic readiness plus actual PDP and sleep soak |
| SMS receive | PARTIAL | Partial inbound segmented radio messages | Full user-led app delivery |
| SMS send | FAIL | SMS previously delivered physically, Android ACK timed out120s | Positive OEM status without false success or duplicate sends |
| MMS send | PENDING | App and MMS APN available | Real MMS PDP/MMSC/send status |
| MMS receive | PENDING | MmsService installed | Real incoming MMS decode/download |
| CarrierConfig startup readiness event | PARTIAL | G379 integrates G374 and physically reached mDataConnectionState=2 on Plus after cold boot without manual event | Additional network/SIM contexts, release acceptance |
| Global MCC/MNC/MVNO APN | PARTIAL | G369 EF-SPN source built 28 assertions | Integrated boot-classpath deployment + world SIMs |
| GPS GLONASS assisted | PARTIAL | G385 real BCM4752 satellite status max14 visible/no true position, G386 NTP cache NULL and7 request failures. G387 real rmnet1/32 offlink gateway netd ENETUNREACH101, scoped Java route patch; G388 selected vendor /misc OTA p11, G389 p11 enforcing misc label; G396 no-DNS-IP HIDL AGPS callback correction, G397 original fixed-size FD_SET highFD ASan stack overwrite fixed via poll. ALL cumulative signed TESTKEY full G397 SHAe949e7ff... on tablet verified root0600 but **not installed**. | Manual owner real TWRP G397 preflight then owner signed full G397 install; physical VALIDATED internet, GNSS UTC/NTP/SUPL + outdoor GPS3D required |
| Infrared IR | PARTIAL | Live HIDL carrier 30-60kHz read-only PASS | Real transmission with receiver |
| Accelerometer | PASS source/static physical, owner dynamic pending | G399 selected OEM3G8 K3DH=y and K2DH=y causes original k3dh.c K2DH position5 diag(1,-1,-1), C compiled5 XYZ vectors; true modern Android11-visible lt01-common HAL uses actual G379 IIO mount_matrix and selected Linux6.12 DTS identical, 16/16 wrong macro/matrix/physical claims rejected Git619ab7e. Active WindowOrientationListener measured framework events, acceleration xyz scale0.009806. | Owner supervised physical rotations all 3 axes still pending; do NOT swap X/Y as wrong K3DH-only branch |
| Ambient light | PASS (limited) | G398 real CM36653 IIO, Android11 AutomaticBrightnessController active and receiving events, selected stock I2C0x60, no new firmware required. | Owner brightness trend across ambient illumination and display throttling |
| Proximity | PARTIAL | G398 real CM36653 IIO chip, Android11 proximity HAL device enumerated, matched stock 3G8 I2C0x60 and selected modern low8/high10 threshold source+physical metadata checks. | User hand/cover near-far real wakeup/power/suspend, no untested blanket PASS |
| Compass / magnetometer | PARTIAL | G398 real Yamaha YAS532 IIO, Android11 magnetic field sensor enumerated, selected 3G8 stock I2C0x2e orientation YAS532_POSITION_7, modern IIO matrix diag(-1,+1,-1), scale 0.000010000 across xyz, virtual AOSP GeoMag Rotation Vector, source16 mismatch tests PASS. | Real magnetometer sample plausibility, true 3-axis portrait/landscape magnetic heading/calibration and original axis layout with owner; enum is NOT physical compass accuracy PASS |
| Hardware keys | PARTIAL | Basic UI known | Power/Home/Vol remap/full wake |
| Magnetic Hall smart cover | PARTIAL | G384 live gpx2(4) hall effect sensor IRQ160, input gpio-keys SW_LID bit0, getevent switch=0000. Stock CONFIG_SENSORS_HALL=y (legacy SW_FLIP GPX1(5) alias conflicts with MAX77693). lid_behavior=0 preserved deliberately; host test + 7/7 fault controls PASS | User-present real magnet open/close, polarity, wake/sleep and opt-in system preference behavior; do NOT force lid_behavior=1 |
| Charging and fuel gauge | PARTIAL | Battery100%, temperature healthy; stock G383 audit confirms MAX77693/MUIC irq GPIO_IF_PMIC_IRQ=GPX1(5) preserved in Linux6.12 DTS | Charging/OTG plug events, charge/discharge safety and cycles |
| Thermal HAL/throttling | PARTIAL | HAL2.0 and sensors working | CPU/GPU thermal endurance |
| Deep sleep and resume | PENDING | G379 normal reboot kernel pstore cmd=shell reached Samsung SEC reset. Recovery reboot timed out20s and shut down. G388 proved vendor fstab missing /misc though factory OTA p11 exists; AOSP init early abort before kernel reset. G389 proved p11 live SELinux block_device instead of misc_block_device; AOSP init grants write only misc_block_device. G389 narrow fixes compiled/signed staged ONLY. | Owner hardware TWRP and guarded G389 installation first; then prove vendor /misc, OTA p11 misc_block_device and supervised recovery reboot, later battery/suspend |
| RTC and network time | PARTIAL | G386 system year2026, GNSS ON_DEMAND_TIME0x16 IgnoreJniTime=false, physical NTP cache null +7 helper failures. G387 confirmed failing netd gateway route ENETUNREACH101 on rmnet1 /32; guarded source fix compiled but NOT installed. | Reestablish mobile default route via coherent ROM, then NTP cache and GNSS native inject-time; no raw system clock change without trustworthy data |
| Media audio codecs/encoder | PARTIAL | H264 decoder proven | Encode, AAC/MP3 and seek/endurance |
| Low-memory performance | PARTIAL | LZ4 ZRAM + lite-ROM | Measured app performance and OOM |
| Internationalization | PARTIAL | Stock Android11 locales, G369 carrier helper | RTL/IME/regional/dual network matrix |
| Full GitHub release / documentation | BLOCK | G379 source commits c3eb194, 4554b3f, c0b4aae local only, private OEM inputs excluded, live postflight documented | Authorized GitHub repositories/remotes, licensing, reproducible signed release |

G403 **Transient old legacy CP SUPL CLOSED -EIO bounded retry and current physically staged signed firmware**: exact original G401 native C single transient failure leaves CP OPEN pending forever if no new event; G403 worker retries max3 under serialized CP mutex and up to2x100ms pause, eight corrected source scenarios PASS, older G396 noIP10+10, G397 highfd ASan9, G400 orphan9 and G401 OOM8 PASS. New G403 full signed developer TESTKEY OTA484326152B SHAb6df8dcab3af2c2afd41271f607f90ed511ce6df342fff172cfa6794b355a1b9 on tablet SHA PASS root0600 NOT INSTALLED. Only new GPS ELF and 3 generated system/cache/userdata images differ versus G401, nine OEM unsafe mutations rejected, signed SYSTEM1122914304B byte QA PASS, BOOT/TWRP/RIL/ART/audio/SELinux unchanged. Latest G403 only TWRP readonly guard3029B SHA6459d29d... device SHA PASS unexecuted, afterinstall G403 readonly audit4213B SHA42496b79... device SHA PASS unexecuted. Physical live G379 Enforcing Plus LOADED GNSS idle. Owner manual original TWRP and separately approved whole G403 install then mobile validated+NTP/SUPL/UTC+outdoor GPS3D. Git3f54f22/60b5597.

G402 **Physical older G379 passive check and future G401 after-install EXACT SOURCE acceptance (NO G402 ROM)**: read-only shell4218B SHA8a21d1... on physical tablet root0600 full SHA PASS, host 1 healthy mocked full G401 +7 failclosed wrong ROM/label/fstab/model/version +3 pending cellular/GPSD fixture cases PASS. Real G379 passive execution once yielded EXPECTED PASS5 FAIL3 PENDING5: global Enforcing, old gpsd, rmnet1 /32 present, but latest G401 gps.default exact SHA, selected vendor /misc p11 and p11 misc Selinux absent because G401 not installed, modem default route/VALIDATED/NTP/SUPL/UTC/outdoor GPS3D unverified. No modem/network/GPS activation or user addresses displayed. Owner manual old TWRP full G401 readonly gate then separately approved install G401 and run G402 after boot. Git6acd9de/926d2ef.

G401 **Native SUPL deferred worker calloc/pthread creation failure silently suppresses replacement framework CP OPEN (now fixed)**: previous local CP remains local_data_open=1 when worker calloc fails, callback immediate framework fallback but actual v332 framework OPEN returns success without new CP because sees stale local flag. Real original G400 C from Git ca84644 reconstructed in deterministic fake CP test reproducibly fails; G401 clears former owner and preserves serialized pending CP CLOSE before fallback, eight corrected real C scenarios oldCP/none × calloc/thread error × framework OPEN/early RELEASE PASS, older G400/G396/G397 regressions PASS Git 4d454c8/fef6c90. Full signed developer TESTKEY G401 ARM32 ROM484325298B SHA d7a8ced2... physically staged device root0600 full SHA PASS, only FOUR allowed targetfile changed contents and9 bad protected OEM mutations rejected, exact SYSTEM1122914304B independent bytes QA, original BOOT/TWRP/CP-RIL/audio/ART/SELinux untouched; latest only-G401 old TWRP gate3032B SHA48af3f... tablet readback PASS but NOT RUN. Real G379 still live no reboot/flash, owner TWRP physical then full install and Network VALIDATED/SUPL/native UTC/GPS3D pending.

G400 **Actual Broadcom old CP SUPL OPEN leak on stale replacement worker CANCELLED by RELEASE**: source real G397 callback+workers compiled C deterministic fake CP demonstrates both local/framework prior OPEN owners left CP open with pending_close when new SUPL REQUEST queued but RELEASE bumps generation before it runs. G400 modern vendor gps_proxy.c narrow RELEASE detects pending orphan with no framework active, schedules old serial CLOSED worker, true C nine behavior cases fixed, historical G396 no-IP10+10 PASS Git f563650. Full cumulative G400 developer TESTKEY signed ZIP484325625B SHA94cbd4e1... device physically staged whole SHA pass root0600 but NOT INSTALLED, strict G387/G388/G389/G396/G397 preserved, only four expected targetfile changes and9 bad source changes rejected, BOOT/TWRP/RIL/audio/ART/SELinux untouched, independent SYSTEM1122914304B bytes exact match; new matching original TWRP-only read-only preflight3029B SHAc18d7d79... physically staged SHA pass +8 negative tests, NOT RUN. G379 live. Need owner manual actual TWRP preflight and separately consent full coherent install, then live Network VALIDATED/SUPL/native UTC/outdoor GNSS3D.

G399 **Original stock 3G8 selected K2DH+K3DH compiled matrix confirmed same as physical Android11**: Both CONFIG_SENSORS_K3DH=y and CONFIG_SENSORS_K2DH=y in actual 3G8 OEM defconfig so k3dh.c K2DH #ifdef position5 diag(1,-1,-1) SELECTED, NOT K3DH-only alternate swap XY branch. Compiled actual OEM C five fake vectors, 16/16 wrong branch/DTS/HAL/live IIO mutants PASS Git619ab7e. Actual Android11-visible lt01-common sensors_lt013g.cpp HAL SHA matches device and Debian, reads selected kernel sysfs mount_matrix and applies it once, actual IIO diag(1,-1,-1), selected DTS same; all exact stock source axes parity PASS. No new ROM/OTA needed, K3DH-only axis mismatch G398 concern CLOSED. Owner real movement acceptance still pending. Details docs/G399-SELECTED-STOCK-3G8-K2DH-MATRIX-COMPILED-PASS.md.

G398 **Selected OEM 3G8 sensor IIO physical identity and Android11 4-sensor HAL**: selected stock K3DH0x19 position5, YAS5320x2e position7, CM36653 0x60; real IIO lis2dh12_accel, yas532, cm36653 on live G379, Android11 all four hardware sensorservice descriptors+synthetic GeoMag, real active light/accelerometer event presence, YAS scale 0.000010000 and mount matrix diag[-1,+1,-1], proximity thresholds 8/10, source16 wrong selected OEM/runtime mutations rejected Git50a890f. Dynamic stock X/Y orientation, true near/far and compass calibration not accepted, no new ROM build. More details docs/G398-STOCK-3G8-SENSOR-IIO-REAL-PHYSICAL-20261017.md.

G397 **Real high-FD SUPL fd_set stack overflow and full signed cumulative staged ROM**: Original G396 modern GPS proxy FD_SET(fd2048) writes beyond fixed-size stack fd_set, tested with exact real git89cfc09 C and clang AddressSanitizer stack-buffer-overflow; G397 only source vendor gps_proxy.c changes to poll/POLLOUT 700ms with no arbitrary FD bound, nine functional ASAN tests PASS, G396 missing DNS IP SUPL callback test10+10 still PASS. Physical G379 GNSS HAL currently maxFD7 (latent bug, not proved position fix). ARM32 GPS ELF SHA4047b595..., coherent full targetfiles ZIP1149694252B SHA7f6e9a... four changed contents vs G396, original factory BOOT/TWRP/Samsung gpsd/libsec-ril/audio/telephony ART/compiled SELinux preserved, 9 protected mutants PASS. Full guarded TESTKEY signed OTA484324885B SHAe949e7ff... device full SHA verified root0600, independently 1122914304B new SYSTEM bytes checked, G397 only original TWRP readonly gate3035B SHA53f6a0... on tablet SHA pass +8 negative cases, NOT EXECUTED. G397 NOT INSTALLED, owner recovery first; no GPS 3D accepted.

G396 **Real SUPL unknown-IP callback source correction, cumulative signed full ROM physically staged**: Previous vendor GPS proxy sends AF_UNSPEC if old Broadcom SUPL request/release has no original IP and DNS-cache failed; AOSP HIDL1.0 AGnss.cpp DROPS AF_UNSPEC. New only-SM-T311 proxy uses AF_INET+INADDR_NONE callback marker (existing JNI converts to null Java host) and clears marker to zero before local TCP probe; ordinary Android cellular SUPL fallback remains possible without invented address. Compiled actual C source tests10 behavior+10 wrong variants PASS, ARM32 built Git5f4034f. Full G396 signed TESTKEY 484326131B SHA a6fcf6db... includes G387 modem route/G388 BCB vendor fstab/G389 p11 SELinux and new G396 GPS, verified 1122914304B SYSTEM bytes vs targetfiles, all original BOOT/TWRP/RIL/audio/telephony/SELinux retained. Real tablet G396 ZIP SHA matches host 0600, separate new G396-only read-only TWRP preflight SHA92d3a6f3... staged0600, 8 negative synthetic tests PASS, NOT RUN. No physical install, internet NTP SUPL or GPS3D acceptance. Old signed G389 only HOST, full private G389 on tablet untouched.

G395 **GNSS assistance UTC original BCM4752 and physical network namespace**: Real G379 system_server, GPS launcher and jailed gpsd all use SAME Linux net namespace as init, not a second network namespace. Android11 NtpTimeHelper fresh-cache -> JNI -> HIDL1.0 -> gps_proxy LT013G_CMD_INJECT_TIME -> original API25 gpsd inject_time preserved; original immutable stock gps.xml IgnoreJniTime=true corrected by actual launcher to false before starting, SUPL enabled and private host DNS sockets bound. Source+real bounded GNSS netns gate G395 19/19 unsafe mutations rejects, Git21ed938. Previous NTP cache null and live rmnet1 missing /32 gateway route are still physically open until G389 installation; delivered UTC rc0, SUPL and GPS3D NOT accepted. See G395 handoff/doc.

G394 **Real G379 successful TWRP full OTA SAME updater + factory SYSTEM bounds**: Exact previously physically installed G379 signed updater-binary/updater-script/SYSTEM transfer map byte-identical to signed staged G389 (full system payload differs). Real G379 TWRP recovery.log wrote280977 blocks and1150881792 SYSTEM bytes EXIT0. Factory p20 4366336 sectors=2235564032B=545792 4k blocks; G389 transfers274149 NEW+6828 ZERO+264815 ERASE exactly545792 nonoverlapping extents, BOOT within real8MiB. G394 source test 15/15 malformed/out-of-bounds transfer variants rejected, Git bae3edd. This is historical physical precedent, not proof current G389 installed, new filesystem services working or GPS3D lock. See G394 handoff/doc.

G393 **Actual signed G389 static updater-binary versus old physical TWRP**: Real signed update-binary ARM32 EABI5 LE softfloat ET_EXEC statically linked (no PT_INTERP/PT_DYNAMIC), exact physically SHA-matched old TWRP sbin/twrp ARM32 same EABI/float and its own /sbin/linker. Signed device whitelist lt013g,lt013gxx,SM-T311; G389 guard PRE -> old dw_mmc SYSTEM block_image_update -> BOOT write -> guard POST, no recovery/CP/EFS/OTA p11 target. Regression source G393 15/15 unsafe in-memory mutations rejected, Git device cdc8700. This proves SOURCE/ELF parity only, NOT working old recovery kernel syscalls, real mount/alias, full ROM install or GPS fix. See G393 handoff/docs.

G392 **Selected original SM-T311 3G8 battery charge/thermal policy**: Actual stock 3G8 defconfig FUELGAUGE_MAX17050+CHARGER_MAX77693_BAT, not alternate 7/10-inch sibling battery ADC thermistor tables. Modern MAX77693 I2C0x66 GPX1(5) IRQ and MAX17050 I2C0x36 GPX2(3) interrupt source matched. Existing source charger 4.35V, USB500mA AC1.8A,6h timer, topoff250mA/40min, pack overheat fuel gauge 50C/4.5V, while charger internal die thermal regulation100C is NOT safe pack temp. Live G379 power_supply Battery health Good Full100%, 26.8C,4.325V,cycle_count139, charger Full online. Source test G392 15/15 wrong sensitive mutants rejected; source commit e6061dc, no kernel binary change. Physical protective trip and actual charging cycle accuracy untested, do not induce battery heat.

G391 **Samsung exact S5M RTC + BCM4752 GNSS 32768Hz LPO factory-parity**: Stock SM-T311 CONFIG_TARGET_TAB3_3G8, CONFIG_RTC_DRV_S5M, REGULATOR_S5M8767, S5M8767_OSC and GPS_BCM4752 confirmed; modern S5M PMIC@66 clock en32khz_ap supplies BCM4752 driver via devm_clk_get_enabled, active GPIO GPL2(2), RTC SoC disabled. Physical live G379 rtc0 name s5m-rtc s5m-rtc, hctosys=1, year2026, GPS power idle0. 10 deliberately wrong variants rejected by tools/test_g391_stock_s5m_rtc_gps_lpo_parity.py source commit b4177b1; no source kernel patch needed. RTC /proc alarm pending=no; debugfs clocks not readable, so physical 32768Hz rate, wake alarm, network NTP and GPS 3D lock NOT measured. No RTC alarm/time/clock/radio change. See docs/G391-FACTORY-RTC-GNSS-32768-LPO-STOCK-PARITY.md.

G390 **REAL INSTALLED TWRP / G389 recovery-only read-only preflight**: Full installed 8MiB TWRP SHA f818c2 real archived image CPIO451 entries; BusyBox/sh/twrp/recovery and necessary applets proven, TWRP FSTAB uses old dw_mmc/by-name whereas Android G379 dynamically uses soc/12550000.mmc/by-name. Signed G389 guard matches old TWRP aliases and exact BOOT 3339x2048 blocks, but live TWRP alias creation and /data mount are NOT verified until owner enters recovery. G390 non-installing read-only shell safety check tests exact p9 BOOT prefix, p20 SYSTEM aliases, unmounted SYSTEM, signed G389 ZIP SHA, rejects wrong environment; synthetic 1 valid and 8 fail-closed tests PASS, physically staged script mode0600 SHA verified, NEVER executed on current G379. No ROM rebuild needed; see G390 handoff and docs.

G389 **Recovery BCB SELinux second blocker**: Real G379 factory OTA p11 (/dev/block/mmcblk0p11) is labeled block_device, but AOSP init only has misc_block_device:blk_file w_file_perms and dev_type read. G388 fixed missing vendor /misc, but still cannot reliably write BCB under Enforcing without p11 label. Common commit016be53 adds only p11 misc_block_device, no blanket grants or EFS/CP/USERDATA relabel. Exact-source 10 mutants PASS; ARM32 sepolicy_tests and vendor_file_contexts_test plus G389 target-files gate proved ONE new compiled label and immutable BOOT/TWRP/RIL/audio/GPS policy/ART. Signed full G389 SHA339d7e2ac87cfdacd6957c92450fe2a503902ac334c6c53204f07474f344c09b physically staged/readback, NOT INSTALLED.

G388 **Android11 recovery bootloader-message BCB vendor-fstab fix**: Physically /vendor/etc/fstab.lt013g contains no /misc; actual PIT OTA p11 alias exists, while unused ramdisk fstab and OEM recovery have OTA /misc. AOSP GetFstabPath chooses vendor fstab on normal boot, bootloader_message get_misc_blk_device fails without /misc, init returns early on failed recovery BCB write; Android 20s reboot timeout observed historically. Source common commit26ce6e0 adds only OTA p11 /misc emmc in active vendor fstab; fs_mgr_mount_all skips raw emmc. G388 source 10 deliberately-bad FSTAB controls PASS, 10+4 G387 Java route tests PASS, full ARM32 target-files and cumulative guarded signed G388 OTA SHAa6ded4e7... physically staged/verified, NOT FLASHED. Actual recovery fix pending physical installation. See G388 handoff and device docs.

G387 **XMM6262 PDP /32 route source correction**: physical ConnectivityService log `networkAddRouteParcel for gateway ... Network is unreachable (101)` on real rmnet1 /32 with separate nonzero gateway: network-agent LinkProperties has default via gateway but kernel netd route tables show just self /32 and dummy0. AOSP installs non-gateway routes first; new `addLt013gPdpGatewayHostRoute` in frameworks/opt/telephony commit717c949 adds direct host route ONLY for exact SM-T311/rmnet1/IPv4/32 nonzero distinct gateway. Exact-source Java tests10/10 +4 failing mutant guards, complete ARM32 telephony-common +target-files PASS, G387 strict G382 immutable BOOT/TWRP/RIL/audio/SELinux and classes.dex gate PASS. NO FLASH, GPS fix/UDP NTP still unproven. See source docs/G387-XMM6262-PDP32-GATEWAY-ROUTE.md and G387 handoff.

G386 **GPS A-GPS/NTP/network diagnosis**: `device/samsung/lt013g/docs/G386-GPS-ASSISTANCE-NTP-NETWORK-ROUTING.md`, 32-bit NTP probe host/device selftest7/7, physically both target DNS_FAILED, Android NTP cache null and 7 requestTime failures, GPS time cap0x16 and jailed IgnoreJniTime=false; TelephonyRegistry state2 and rmnet1 IPv4 but no nondummy default route. Bounded source+live gate `test_g386_gps_assistance_routing.py` 8/8 negative fixtures PASS. Not a modem root-cause or GPS fix proof. Keep GPS first, investigate safe ConnectivityService routing.

G385 **GPS-FIRST actual physical GNSS hardware gate**: source `device/samsung/lt013g/docs/G385-GPS-BCM4752-UART-IDLE-PHYSICAL-AUDIT.md` and two read-only coordinate-free GNSS status traces under `artifacts/SM-T311-G385-GPS-FIRST-20261016`. Android foreground test app T311GnssStatus had two 70s sessions and was uninstalled; 14 visible SV/132 callbacks then 7 visible/71 callbacks, C/N0 up to32dB-Hz, 0 used, 0 fixes; power returned0, SIM stayed LOADED. Full positional fix still unverified. This is top priority; **new canonical autonomous order** for all20 immutable IDs in `chat-history/AUTONOMOUS_PRIORITY_QUEUE.md`; public GitHub/licenses last and non-blocking.

G384 **Hall/PMIC live IRQ gate**: source `device/samsung/lt013g/docs/G384-STOCK-HALL-MUIC-PHYSICAL-IRQ.md` and 7 negative-control test `tools/test_g384_hall_pmic_runtime_contract.py`, actual IRQ ownership confirmed, input SW_LID enumerated only, no magnet acceptance. User `lid_behavior=0` must remain unchanged.

G383 **stock reference gate**: `device/samsung/lt013g/docs/SM-T311-STOCK-FIRST-HARDWARE-PARITY-G383.md` (selected 3G8 defconfig vs modern ports); compiled/host no-ROM-write check `tools/test_g383_stock_sm_t311_contract.py` PASS. Original GPS BCM4752 driver must NOT be confused with sibling/unselected geofence hostwake code that shares PMIC IRQ GPX1(5). Canonical status is this table, not that separate source audit.

Fixed physical limits: SM-T311 is GSM/UMTS 2G/3G only. No software can add LTE, VoLTE, unsupported RF bands or restore carrier shutdown networks. T315 is a different model. User APN overrides must remain possible.

G373 last physically validated: BOOT SHA f9292bab437bce12cea56e6e0c067a15c87475b69a61fc0c13fd4d3f07b9694f, matching G375 OTA BOOT byte for byte; RECOVERY SHA f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f matches G375 ROM; Android11 Enforcing, Plus LOADED UMTS, protected RIL719750b0f544238c0986ecbbc8c7d4b59b822f00c7ba1c883809b1b54fcee491. G371 historically demonstrated HTTP301 after manual wake; G373 cold PDP NOT accepted: effective carrier config IS applied (read-only cmd phone cc get-value), but DcTracker missed readiness event. G373 bootstrap 75sec AUTO-restored. G374 diagnostic SINGLE valid CarrierConfig broadcast without SIM/USB changes restored PDP cause0 + real HTTP301. NEXT deploy G374 generic readiness source with fully regenerated ART and verify cold boot, then owner voice/SMS/MMS.


## Source-to-source Samsung stock ↔ Linux 6.12 / Android 11 traceability (verified code paths, NOT complete function acceptance)

| Stock Samsung file | New implementation | Comparison / remaining gate |
|---|---|---|
| kernel-stock-samsung/drivers/misc/modem_if/modem_link_device_hsic.c | kernel/drivers/misc/samsung-tab3-xmm6262-runtime.c | Original link_pm_runtime_start, link_pm_runtime_get_active, IRQ/HOSTWAKE compared to G370–G373; startup PM grace tested but data session not accepted |
| kernel-stock-samsung/arch/arm/mach-exynos/mach-tab3.c | kernel/arch/arm/boot/dts/samsung/exynos4212-tab3-3g8.dts + exynos4212-tab3.dtsi | Exact SM-T311 board topology, regulator/GPIO/I2C/MMC/USB checks; verify live peripherals and pressure/thermal |
| kernel-stock-samsung/arch/arm/mach-exynos/tab3-input.c | exynos4212-tab3.dtsi gpio-keys, touchscreen stack | Buttons/panel touch source coverage; physical long-run buttons remain |
| kernel-stock-samsung/arch/arm/mach-exynos/tab3-jack.c | modern ASoC plus Android audio routing | Headphone detect, headset mic/buttons and call audio acceptance remain |
| kernel-stock-samsung/arch/arm/mach-exynos/tab3-power.c and board-tab3-battery.c | exynos4212-tab3.dtsi MAX77693 and MAX17050 nodes | Charging/fuel gauge seen, deep sleep/thermal long tests missing |
| kernel-stock-samsung/arch/arm/mach-exynos/board-tab3-gps.c | exynos4212-tab3-3g8.dts BCM4752 power and GPS UART pinctrl | Real GNSS SV/fix not yet accepted, stock geofence IRQ logic distinct |
| kernel-stock-samsung/drivers/misc/modem_if/* | private OEM Samsung libsec-ril + protected Android11 vendor/lib/libril.so G368 | Exact RIL7 IPC PIN/PUK size & once guard cross-checked, still actual voice/SMS ACK/MMS pending |
| AOSP Android11 frameworks/opt/telephony DcTracker.java baseline | source G369 carrier-neutral APN sorter and G374 CarrierConfigReadinessRetry | Android docs explicitly say carrier config may reload after broadcast, need bounded retry not hardcoded MCC; compiled and ART parity required before deployment |

IMPORTANT: Stock software implementation, modern driver availability, actual registration and end-to-end user function are DISTINCT evidence levels. A successful build, live sensor enumeration or USB device ID does not imply completed test.

DONE10: P0-3 AudioFX, P0-4 Trust, P1-7 GPU, P1-8 SF/SystemUI, P1-9 MediaProvider, P1-12 H264, P1-15 ZRAM, P2-16 sensors baseline, P2-17 Linux6.12, P2-19 Lite ROM.
TODO10: P0-1 SELinux, P0-2 release, P0-5 OEM licensing, P0-6 OTA parity, P1-10 voice/SMS/MMS/data coldboot, P1-11 GPS/GLONASS, P1-13 peripherals, P1-14 suspend/thermal, P2-18 ARM32 integration, P2-20 final acceptance.

## G375 full ROM source/stock parity result (OFFLINE; NOT FLASH READY)

- Full target-files rebuilt using actual physical G373 GPL kernel+DTB in *SM-T311-only* BoardConfig, preserving common Tab 3 siblings; BOOT SHA f9292bab... and RECOVERY SHA f818c29f... exactly match physical partitions, physical tablet never flashed in G374/G375. Device Git d816fcd. Local prebuilt intentionally Git-ignored due OEM binary licensing boundaries.
- Final package ZIP SHA9c07a0f31390bc7885efeedc363418180f89960a03067b847fc30e854e235519, size1,149,691,994 bytes. 41 exact original live paths; expected 3 replacement SystemUI files internally APK DEX/VDEX CRC equal, but physically untested. Fourth changed file rild has 23 allocated ELF sections, code/data section hashes all identical except .note.gnu.build-id; SHA still not equal, audit Git5578343 with 4 unit tests.
- 17 essential archive entries validated (BOOT, RECOVERY, SystemUI triple, RIL + Samsung OEM, telephony-common + ART trio, Dialer, Messaging, MmsService, TeleService, CarrierConfig, APN database). Inclusion does NOT prove functionality.
- SELinux policy hash unchanged but still explicitly permissive in su, rild, lt013g_gps_legacy, backuptool; global Enforcing alone is insufficient for final all-domain certification.
- G374 CarrierConfig generic source compiled ARM32 + regenerated ART, not yet installed. Until complete final gate passes, current physical G373 + G368 remain live. Exact machine report artifacts/SM-T311-G374-CARRIER-READY-20261016/G375-FULL-RELEASE-INTEGRATION-REPORT.json.

## G376 stock-first GPS SELinux and actual integrated ZIP gate (OFFLINE)

- Stock KitKat GNSS runtime expects /proc, /sys and SELinux status; original Samsung board power & UART retained. New Android11 GNSS launcher uses its own namespace mnt from init. G376 source d286583 replaces three new filesystem mounts (`proc`, `sysfs`, `selinuxfs`) with recursive bind of inherited host /proc and /sys, validates `/sys/fs/selinux/status`, and preserves *private* binderfs/servicemanager (not attached to Android's global binder). No new broad SELinux rules, PIN/PUK/EFS changes, or modem reset. Physically read-only host `/sys/fs/selinux/status` verified existing, as are host proc/sys/selinuxfs mounts.
- G376 exact C++ source-extracted compiler ASan/UBSan4 scenarios PASS; actual Android11 ARM32 `lt013g-gps-legacy-launcher-v195` build job e32f2510994d EXIT0 SHA0da1f56b51956fbcf9e2e38e300fb24cbd049f559d28ca4253ee989a681bda46, previous physical G373 binary SHA7be4ea24061da4214bac0fc96b73e787c02d9840681dfbc38ff5591641f08aa7. **New binary NOT physically installed or functionally GNSS tested**, do not mark it PASS.
- Incremental G376 full ARM32 target-files build job 07317dcae1e4 EXIT0; archive SHA15461cb1eee54577b244c529ca720f07f1b8f5d109cd1538feca67d7842f9242. Exact BOOT/RECOVERY unchanged and physically match. 45 existing hardware inventory files 41 MATCH / 4 intentional DIFFERENT / 0 missing, systemUI preopt DEX/VDEX CRC MATCH; new GPS launcher is an *additional* non-inventory file, verified in ZIP with separate physical/hash manifest.
- G376 release gate device Git090448b + 9576241: 15 host negative/positive regression tests PASS; 11/12 release checks PASS including SHA of ACTUAL 1.15GB ZIP and 18 internal critical payloads. HOLD_SELINUX because domains `su`, `backuptool`, `rild`, `lt013g_gps_legacy` remain permissive. **No flash**; final user's current Plus SIM and G373 RIL preserved. Report artifacts/SM-T311-G374-CARRIER-READY-20261016/G376-INTEGRATED-RELEASE-GATE-ALL-BYTES.json.
- Android11 CDD requires global SELinux + no per-domain permissive; `su` and `backuptool` are userdebug-or-eng-only in original AOSP/Lineage sources, but still count in this userdebug build. RIL domain physically sensitive (owner physical voice final), GPS still requires own binderfs private mount; no blanket allow filesystem mount.

## G377 binderfs SELinux hardening boundary — proven by real compiler

AOSP Android11 system/sepolicy/private/domain.te around line344 explicitly NEVERALLOWs filesystem mount from nearly all domains, including lt013g_gps_legacy. Separate new binderfs mount is architecturally necessary for original Samsung API25 GNSS private servicemanager and must not be replaced with Android global binder; Linux binderfs docs show each mount creates independent devices. An initially scoped experimental binderfs mount/unmount allow (SM-T311-specific offline only) FAILED real `sepolicy_neverallows` build job 1b3de5786f9c with explicit assertion. Removed invalid uncommitted rule and BoardConfig inclusion, recompiled m -j3 sepolicy sepolicy_neverallows job f4f88f5f2b31 EXIT0; SHA2131d3f039b4c1c554d8a38a428b9a8ae5208f3f439ea7dd3d6d28f55f9ec2c2 unchanged. Added source guard and architecture doc Git8337d06 (docs/G377-PRIVATE-BINDERFS-SELINUX-ARCHITECTURE.md). No physical flash or broad SELinux permissions. Next evaluate trusted init-owned binderfs creation in tightly scoped private namespace, and whether vendor init RC commands use separate restricted vendor_init context. Then full GNSS physical proof before enforcing.

## G378 — source-built independent GNSS Binder, exact-paired Android11 full ROM (OFFLINE)

- Original Samsung SM-T311 stock Broadcom BCM4752 API25 gpsd still runs isolated in Android11 mount namespace; never share 2014 servicemanager with platform Binder. Linux6.12 actual binderfs.c and binder.c prove unique binder_device, per-device binder_context. G378 kernel defconfig adds ONLY `lt013g-gps` to `CONFIG_ANDROID_BINDER_DEVICES`; comparison of 1432 enabled config values with G373 configuration confirms exactly **1 changed value**; G378 real ARM32 zImage SHA5cf2630d... DTB b1312536... unchanged, GPL kernel Git2c69912ec. New hardware candidate NOT installed.
- G378 GNSS launcher source Git79ff88c uses existing /dev/binderfs/lt013g-gps (auto-created by trusted Android11 binderfs mount during init), chown system:gps 0660, private MS_BIND into GNSS jail /dev/binderfs/binder. No new binder filesystem mount, no binder-control ioctl, no chmod/chown/overwrite of main /dev/binder, hwbinder or vndbinder. Verifies different major/minor from main Binder, unchanged global binder inode, fail closed if no new Binder device. Source-extracted actual C++ ASan/UBSan9 cases PASS; actual Android11 GNSS ARM32 binary SHAb68a7a3... build EXIT0; staging6 host tests, kernel/launcher pair7 host tests PASS.
- G378 SM-T311-only BoardConfig references ignored local GPL zImage+DTB prebuilt SHA d55e8091... built from kernel2c69912ec, not OEM Samsung proprietary firmware, shared T310/T315 BoardConfig untouched. Full Android11 userdebug target-files ZIP SHA7ab50d26be268f6c67e44aa21e60bdb55221435e29191f2e7ecf052d76328061, new BOOT SHA cad5e76faf372beff3243ec615924286096713c994a6e5a03d3231f69478e6fd, exact GNSS pair PASS, real 18/18 ZIP critical payload hashes PASS; physical G373 BOOT SHA f9292bab... DIFFERENT (intentional future update). Source G374 generic 2s CarrierConfig retry + coherent ART remains OFFLINE.
- G378 strict release gate 10/12 PASS: blockers physical BOOT mismatch (new kernel not yet accepted) and four permissive SELinux domains; full release NOT FLASH READY. Do not replace live G373 kernel or protected RIL G368 without comprehensive recovery/rescue, safe first boot and GNSS tests. Archive and release QA under artifacts/SM-T311-G378-BINDERFS-20261016/.

## G379 -- real full signed OTA, not yet installed because boot-to-TWRP failed

User authorized prompt development installation with sensible privileges. AOSP standard full OTA build initially failed because G378 target_files missed recovery.fstab; fixed by non-destructive targetfiles copy with exact SM-T311 original fstab. Signed G379 testkey OTA generated. Byte-verified 1,122,914,304 SYSTEM bytes against target sparse system image PASS. Physically uploaded original signed unguarded OTA to /data/local/tmp/SM-T311-G379-FULL-OTA.zip on live G373 and verified SHA1acaaea5025a6a98de884f08c97873f28559b8a3f4224148c55320c966145ded; **this old staged package must NOT be installed**. ADB reboot recovery failed; Android ShutdownThread logged Reboot failed at 21:36:18 then powered off. No physical SYSTEM/BOOT writes; ADB now EMPTY.

Prepared safer fail-closed GUARDED signed OTA on Debian only: artifacts/SM-T311-G379-LIVE-OTA-20261016/SM-T311-G379-GUARDED-BOOT-GPS-CARRIER-TWRP-SIGNED.zip SHA256 6bda8b1d12eb431cc31646e3b32f19f75bdb27ab4f0c69d3fb8dcffa3299a834. TWRP precheck enforces exact previous BOOT f9292bab... and mmcblk0p9/20 partition aliases; postcheck confirms new BOOT cad5e76... . Nine real shell mock tests PASS, guarded signed OTA SYSTEM 1.12GB byte parity PASS, recovery untouched, no /data wipe, G368 PIN/PUK protected RIL included. Device source Git11678ab. User physical action needed: hold Volume Up + Home + Power to boot TWRP. Then via ADB verify recovery, transfer NEW guarded ZIP, check on-device SHA, install, first boot acceptance. Full canonical tracker chat-history/DEPLOYMENT_LEDGER.md, handoff SM-T311-G379-SIGNED-OTA-REAL-INSTALL-BLOCKED-BY-REBOOT-20261016.md.
