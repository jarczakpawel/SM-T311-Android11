SM-T311 G657: actual herd7 LKMM execution, 5 SM-T311 illustrative models
============================================================================

Date 2026-10-20. Physical SM-T311 remains G650 kernel 7.3.0-pjarczak,
Android11. Experimental no-reference RCU remains default OFF.

herdtools7 version 7.58 is now installed and executed successfully. The
unmodified invocation initially failed: Cannot find file linux-kernel.cat.
To use the EXACT linux-kernel.cat/.bell/.def in OUR kernel tree, add:

    herd7 -I <kernel>/tools/memory-model -conf <kernel>/tools/memory-model/linux-kernel.cfg <litmus>

tools/testing/selftests/smt311/lkmm_prepare.py now automatically uses the
local -I path, requires a successful upstream full-fence SB smoke test
(Never 0/3) and validates each expected local observation and witness
accounting, not just herd7's successful exit code.

ACTUAL LKMM observations on herd7 7.58:

SMT311+PubWmbRmb+FlagData         Never      0 positive, 3 negative
SMT311+NoWmb+FlagData              Sometimes  1 positive, 3 negative
SMT311+AddrABA+FlagSizeReload      Sometimes  6 positive, 66 negative
SMT311+IsizeSeqcount+32Smp         Never      0 positive, 18 negative
SMT311+IsizeNoSeqRetry+32Smp       Sometimes  3 positive, 9 negative

The paired controls behave as intended. The 32-bit i_size seqcount litmus
checks a single iteration and a constrained non-torn outcome, not the
whole C read_seqcount loop on the actual Cortex-A9 compiler output.
The ABA litmus uses an abstract allocation generation as an oracle,
not a kernel field. Its permitted executions do NOT prove an actual
Linux memory corruption, use-after-free, or proof that reference-free
RCU ON is safe. Models omit full XArray spinlocks/node lifetime,
PG_rcu_free atomic bit semantics, inode eviction, delayed folio reuse,
complete writer callgraph, RCU grace and HighMem cache alias coherence.

New models: tools/memory-model/litmus-tests/SMT311+IsizeSeqcount+32Smp.litmus
and SMT311+IsizeNoSeqRetry+32Smp.litmus; three earlier G654 model files
still used. Machine-readable observations and RAW herd7 output retained
in project ledger G657_HERD7_SM_T311_ALL5_MACHINE_RESULTS_20261020.json
and G657_LKMM_FIVE_MODELS_VERIFIED_20261020.log.

MANDATORY STATUS:
S0 PASS; S1 ACTIVE (23 writer/readers with 17 structural checks);
S2 BLOCKED (executed litmus is not complete folio ABA and LKMM proof);
S3 BLOCKED (HighMem lifetime/cache alias);
S4 BLOCKED (no physical RCU ON); S5 BLOCKED; S6 BLOCKED; S7 PARTIAL.

NO boot/kernel runtime changes or flash, no backups, no auto PIN, no
audio/camera tests during sleep, owner-only GitHub publish.

NEXT: expand the LKMM model to exact actual page allocation free,
atomic bitops, XArray locking, 32-bit seqcount, and safety during
truncate/migration. Only after true S2 and S3 proofs consider RCU ON.
Every next Samsung project report MUST REPEAT DONE / BLOCKERS / NEXT /
PHYSICAL TABLET and entire S0-S7, with instruction to repeat again.
