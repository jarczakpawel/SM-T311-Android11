G713 — SM-T311 G700 reference-backed read release contract
==========================================================

2026-10-21. This is a linked **ARM32 ELF** release gate, not a
source-only guess, not a fresh BOOT build, and not a physical test.

Reproduce after the G686 and G700 host builds are present::

  make -C tools/testing/selftests/smt311 check-release-rcu-off

This target executes rcu_reference_binary_contract_g713.py and never
builds or flashes a device image. Machine results:
Documentation/mm/sm-t311-rcu-reference-binary-g713.json.

23/23 positive checks PASS, eight altered source/ELF input controls
detected. Two of the controls synthesize altered disassembly inputs;
they do not alter real ELF files.

Real ELF32 ARM comparisons
--------------------------

Both host images are ARM 32-bit linked vmlinux. The G686 experimental
build has filemap_read_rcu of 1640 bytes / 410 decoded ARM
instructions; the G700 RCU-OFF release image has an eight-byte
two-instruction zero-return stub:

  mov r0, #0
  bx lr

Neither G700 filemap_read() nor G700 shmem_file_read_iter() contains
a branch/call to filemap_read_rcu. Conversely, G686 experimental
filemap_read() links an actual branch to filemap_read_rcu.

G700 filemap_read() calls filemap_get_pages(), which calls
filemap_get_read_batch(). The latter executes rcu_read_lock,
folio_try_get, xas_reload for pointer validation, and folio_put
when the XArray entry does not match. Normal buffered read later
copies from the still-referenced folio, including flush_dcache_folio
for writable aliases, and releases it with folio_put. Separately
filemap_get_entry() has its own RCU / folio_try_get / xas_reload /
folio_put and retry path.

G700 and G686 have identical linked function sizes for the
reference-backed page provider and batch, but this alone is NOT a
proof of identical machine instructions. Their behavior still
depends on wider mm, architecture, memory and filesystem code.

Performance caveat
------------------

The G694 physically G650-like baseline comparison already showed
that ordinary filemap_read() compiled without the trivial stub
call despite its earlier source form; do NOT claim G713/G687
produced a new measurable filemap_read speed improvement over
G650. G688 did remove the redundant zero-return stub call from
shmem_file_read_iter in the release build relative to its earlier
baseline. These optimizations were implemented before G713.
G713 adds a fail-closed repeatable **release-binary inspection gate**.

Scope and unresolved safety
---------------------------

No new runtime MM kernel C changes, no Android executable change,
no new kernel build, no physical installation. This gate proves
named callsites and reference mechanisms in *host G700*, not every
indirect possible caller or the current physical G650 static key.
It does not close S1-S3 proofs needed to enable no-ref RCU.

G700 8MiB HOST-ONLY BOOT SHA256 remains:
aec9655acc727ab01d0bde3711414f9606c58c5fdcafb6ae5c52a2d3346edc7f.
The current p9 BOOT and p10 RECOVERY hashes are unavailable to
unprivileged Android shell and reliable TWRP recovery is unverified.
Do not flash unattended or turn RCU ON.

S0 PASS, S1 ACTIVE, S2-S6 BLOCKED, S7 PARTIAL, S8 ACTIVE.
G714: evaluate other non-no-ref release performance/safety work
based on measured actual G650 bottlenecks, or pursue supervised
S6 recovery verification. Avoid redundant binary comparisons.
