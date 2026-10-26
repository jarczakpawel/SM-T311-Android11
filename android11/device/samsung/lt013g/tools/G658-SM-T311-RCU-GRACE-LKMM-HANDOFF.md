# G658 cross-repo SM-T311 Linux 7.3 / Android 11 RCU safety progress

Only documentation in Android device source; no Android ROM, HAL, HWC, EGL,
Mesa, RIL/GNSS or SYSTEM change. Physical tablet remains G650 BOOT,
Linux 7.3.0-pjarczak, SELinux Enforcing, RIL running, SIM LOADED.

Kernel source 274141791 extends six? No: exactly SEVEN genuine herd7 7.58
LKMM models with pinned observations. Two NEW grace-period controls:
- SMT311+RCUGraceLastFree: Never (0 positive, 2 negative).
- SMT311+NoRCUGraceLastFree: Sometimes (1 positive, 2 negative).
These show the protection of synchronize_rcu in an abstract RCU pointer
reuse case versus deliberately omitting grace. They do NOT verify the
entire Kiryl/Linus 18-patch reference-free RCU algorithm on ARMv7.

Kernel tools/testing/selftests/smt311/rcu_grace_source_gate.py adds
15 source checks and 3 mutation negative controls for tagged folio
deferred-free release through queue_rcu_work and expedited GP; all pass.
Main unresolved: unmarked old folio reused as new marked folio under
XArray address ABA, inode eviction, page migration/split, HighMem cache
alias and 32-bit SMP seqcount. No RCU ON or flash in G658.

Full evidence and table in project ledger:
G658_LINUS_KIRYL_RCU_GP_PROOF_PROGRESS_20261020.md.
Mandatory plan:
SMT311_LINUS_KIRYL_PLAN_G653.md and kernel
Documentation/mm/sm-t311-rcu-progress.json.

Always repeat in EVERY subsequent SM-T311 project status:
DONE / BLOCKERS / NEXT / PHYSICAL TABLET plus stage table:
S0 PASS, S1 ACTIVE, S2 BLOCKED, S3 BLOCKED, S4 BLOCKED,
S5 BLOCKED, S6 BLOCKED, S7 PARTIAL.
Then remind that the same must be repeated in the NEXT project response.
Keep no-ref OFF until S2+S3 proofs. Do not confuse test-only HEAD with
installed BOOT. Only owner publishes to GitHub. No backups, no SIM auto
PIN/PUK or ringtone/audio/camera while user sleeps.
