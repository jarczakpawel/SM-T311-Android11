# G663 - XArray pointer ABA object identity, not necessarily UAF

Android device source status only. Kernel commits `b0254c08e` and `0fb9b784c`, source-only. No Android executable/overlay/package changes.

G663 explorer `tools/testing/selftests/smt311/rcu_provenance_g663.py` in the kernel enumerates 6435 SC interleavings of one reader and one writer. An initially unmarked old folio, removed and recycled before flag check, may share its numeric address with a newly allocated, marked folio. Six accepted mixed-generation pointer histories copied a live newly marked folio in this abstract source-ordered schedule; **zero** read a freed folio in the modeled protected variant. A *counterfactual* model without tagged-folio grace allowed 12 dead-object copies. A deliberately invalid foreign mapping case was rejected by the real source mapping guard in the model.

Neither pointer-address ABA nor the control witnesses prove a Linux UAF, data corruption, full safety, or exploitable issue. Model is not actual LKMM/herd7 and does not capture ARMv7 weak memory, internal XArray nodes, truncation/eof, cache or complete callgraph. Keep all existing production code unchanged. No-ref RCU stays OFF.

Status: S0 PASS, S1 ACTIVE, S2 BLOCKED, S3 BLOCKED, S4 BLOCKED, S5 BLOCKED, S6 BLOCKED, S7 PARTIAL. Physical BOOT G650 with last verified SHA256 `2d477816fef0dbe2f41ca30747201dbe773e3dc3e710348c118226ee2999b7a3`, kernel 7.3.0-pjarczak. No flash or backup, no modem PIN automation. GitHub upload owner-only.
