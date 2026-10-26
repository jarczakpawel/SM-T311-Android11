# SM-T311 G668 - writable-mmap counter TOCTOU versus ref-free RCU copy

Local kernel commits: `9a396bbd3` (runnable source and bounded SC diagnostic) and `f587a3fb7` (machine progress). Diagnostic:
`tools/testing/selftests/smt311/rcu_mmap_alias_g668.py`.
Results: **16/16 anchored source checks PASS, five deliberately damaged source versions REJECTED**, 70 source-ordered two-thread schedules, five that allow a newly created shared writable mapping to write before a reference-free copy after the reader already observed zero writable mappings.

Source detail: `mapping_writably_mapped()` atomically reads `i_mmap_writable` but does not pin it. `mm/vma.c:mmap_region()` may increment it after the reader's check and before or during its subsequent `rcu_read_lock -> filemap_read_rcu_copy`. `__vma_link_file()` persists accounting for the new VM_SHARED/VM_MAYWRITE VMA. `CONFIG_CPU_CACHE_VIPT=y` and `CONFIG_SMP=y` are set on SM-T311; ARM flush implementation has cache alias-sensitive branches.

This is a **check/use race witness even under SC**, not a demonstrated wrong-byte read, cache-coherency failure, C/ARMv7 LKMM safety result, UAF, or validated root cause. A second unpinned check immediately before copying would still permit the writer to start afterward. An appropriate cure requires correct lock/protocol/synchronization and proof that it preserves mmap, dcache coherency, and practical performance; do not turn it on simply to test.

Device remained physically running baseline G650 Android 11 Linux `7.3.0-pjarczak`, Enforcing RIL running SIM LOADED. Ref-free RCU stays OFF by default. BOOT SHA last verified earlier: `2d477816fef0dbe2f41ca30747201dbe773e3dc3e710348c118226ee2999b7a3`. No Android executable production changes or BOOT flash. No backup/PIN automation/sounds/camera. Host G665 native XArray binary remains unrun due missing liburcu-dev headers.

S0 PASS; S1 ACTIVE; S2 BLOCKED; S3 BLOCKED; S4 BLOCKED; S5 BLOCKED; S6 BLOCKED; S7 PARTIAL. Owner alone publishes GitHub.
