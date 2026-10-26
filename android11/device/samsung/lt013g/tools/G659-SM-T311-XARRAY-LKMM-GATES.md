# G659 - SM-T311 Linux 7.3 XArray/RCU audit (source only)

Date: 2026-10-20. No Android runtime, kernel runtime, BOOT or SYSTEM changes.

Kernel has four new G659 LKMM diagnostic models, with exact actual herd7
observations: XALockUnmarkedABA Sometimes 2/49, XALockGraceABA Sometimes 1/27,
XALockOldNewGenNoGrace Sometimes 2/76, XALockOldNewGenGrace Sometimes 1/41.
Seven source anchors PASS. Source/runtime code matches installed G650.
New models deliberately simplify pointer generation/reuse and are NOT proof
of use-after-free or of a failure of the real kernel RCU grace period.
G658's seven previously verified models remain valid, with different scope.

Physical passive ADB confirmed Android 11 boot1, 7.3.0-pjarczak, Enforcing,
RIL running, SIM LOADED, deferred RCU counters 0. Physical BOOT remains G650,
last-verified SHA256
2d477816fef0dbe2f41ca30747201dbe773e3dc3e710348c118226ee2999b7a3.
No flash/reboot, PIN/PUK, phone, sound, camera, backup or GitHub push.

S0 PASS; S1 ACTIVE; S2 ARMv7 ABA/lifetime BLOCKED; S3 HighMem BLOCKED;
S4 RCU ON BLOCKED; S5 ABBA BLOCKED; S6 selection BLOCKED; S7 PARTIAL.
Next: source-accurate provenance/lifetime proof for originally unmarked folio,
followed by HighMem proof; no activation of experimental RCU yet.
Full kernel document: Documentation/mm/sm-t311-rcu-aba-g659.rst.
Project ledger: linux-7.3-port/G659_XARRAY_UNMARKED_ABA_LKMM_DIAGNOSTICS_20261020.md.
Repeat DONE/BLOCKERS/NEXT/PHYSICAL TABLET and S0-S7 next response.
