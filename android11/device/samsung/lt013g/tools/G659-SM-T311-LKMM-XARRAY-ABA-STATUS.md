# G659 - ARMv7 XArray ABA (source and real LKMM only)

Device: Samsung SM-T311, Android 11, Linux 7.3.0-pjarczak.
Physical BOOT: G650, last SHA256 2d477816fef0dbe2f41ca30747201dbe773e3dc3e710348c118226ee2999b7a3.
The reference-free RCU experiment remains OFF.

G659 kernel HEAD 559cd7553481 contains only LKMM models, runner and documentation; executable kernel code still matches G650 a2fca4558.
Actual herdtools7 7.58 with in-tree Linux model:
- SMT311+XALockGraceABA: Sometimes 1/27
- SMT311+XALockUnmarkedABA: Sometimes 2/49
- SMT311+XALockOldNewGenGrace: Sometimes 1/41
- SMT311+XALockOldNewGenNoGrace: Sometimes 2/76

These are simplified diagnostic results, NOT proof of a Linux UAF. In particular, the grace model also allows a mixed-generation observation, and such an observation need not mean a dangling pointer. The writer-only XArray spinlock does not serialize the speculative read side. Generation is not an actual field validated by filemap_read_rcu_lookup(). G658's separate grace correctness control remains valid.

Preserve the eight stages: S0 PASS; S1 ACTIVE; S2 BLOCKED; S3 BLOCKED; S4 BLOCKED; S5 BLOCKED; S6 BLOCKED; S7 PARTIAL.
Next: exact object-lifetime read/write mapping including folio flags, xa_lock, free/reuse, mapping eviction, migration, truncate and i_size seqcount. Do not enable no-ref RCU or flash documentation-only commits. Keep no PIN automation, no unnecessary backup or sound/camera/call tests while the user sleeps.
Only the owner publishes GitHub.
