# G714 — read-only, physically executed CPU/RAM/ZRAM diagnostic

Source: `tools/g714-readonly-resource.sh`. Pushed to physical SM-T311 Android 11 `/data/local/tmp/g714-readonly-resource.sh` and executed with ADB shell **UID2000**. Device SHA256 exactly `167125f7bd37840f63c03f880f54eb1186b3032db4909f041ba8d171cef69f2f`, matching the source. No sysfs/ROM writes, root escalation, calls, sound, PIN, camera, GNSS, recovery or flash.

Run on an already connected tablet, without rebooting:

```sh
adb push tools/g714-readonly-resource.sh /data/local/tmp/g714-readonly-resource.sh
adb shell sh /data/local/tmp/g714-readonly-resource.sh
```

Kernel-side host verifier: `make -C tools/testing/selftests/smt311 check-passive-resource-g714`; uses archived G714 snapshot, **25/25 checks PASS and 9/9 modified telemetry controls caught**, compares actual Exynos4212 DTS OPP and G700 host .config (does not query device and does not build BOOT).

Physical G650: 893736KiB MemAvailable; 786428KiB SwapTotal/SwapFree; zero pswpin/pswpout, zero oom_kill/direct reclaim/all allocstall; CPU0–1 schedutil 200–1400MHz, 4.07-second CPU time-in-state 61.43% 200MHz and 31.70% 1400MHz; battery 100% USB charging 28.4°C battery sensor. DO NOT confuse CPU frequency time with utilization or battery sensor with SoC thermals. zRAM compressor/mm_stat sysfs UNAVAILABLE under UID2000, not evidence of a broken ZRAM.

No reason to force swappiness/ZRAM/governor/overclock based on this snapshot, no new Android runtime changes. G700 RCU OFF remains HOST ONLY, last physical G650; current BOOT p9 / RECOVERY p10 SHA and reliable unattended TWRP return unknown. S0 PASS S1 ACTIVE S2–S6 BLOCKED S7 PARTIAL S8 ACTIVE. NEXT G715 passive SurfaceFlinger/HWC jank root cause vs safe runtime data. Every report/handoff full four sections and ALL S0-S8; owner pushes GitHub.
