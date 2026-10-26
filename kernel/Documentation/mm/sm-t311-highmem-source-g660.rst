SM-T311 G660: ARMv7 HighMem and RCU copy source gate
=====================================================

Scope
-----
This is a source-only check for the current Linux 7.3 tree, not a device
test, a memory ordering proof, or a runtime performance result.

Evidence
--------
The SM-T311 defconfig enables SMP and HIGHMEM. The experimental
filemap_read_rcu_lookup() rejects both large folios and pages outside
ZONE_NORMAL and checks folio->mapping. A second exclusion occurs in
filemap_read_rcu_can_copy(). filemap_read_rcu_batch() declines the
reference-free copy path for writably mapped files when ARM requires
flush_dcache_page(). ARM advertises ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE.

The copy path uses kmap_local_page()/kunmap_local() inside an RCU read
section with user page faults disabled. In mm/highmem.c, kmap_local_page()
returns page_address() for normal low-memory pages when forced debug
mapping is disabled; HighMem uses a temporary mapping. The ARMv7 cache
type and aliasing flush logic is architecture dependent and cannot be
replaced with a blanket assumption that all mappings are coherent.

The reproducible fail-closed gate is
tools/testing/selftests/smt311/rcu_highmem_gate_g660.py, with results
in Documentation/mm/sm-t311-highmem-source-g660.json.

Limitations
-----------
Checking two ZONE_NORMAL exclusions is not a proof of pointer lifetime.
The lockless reader tests folio flags before the XArray reload and
before the mapping/zone checks. A recycled unmarked donor with an
identical address remains an S2 object-identity obligation. The source
gate does not inspect runtime CP15 cache identification, device DMA,
or real application page placement and cache pressure.

Decision
--------
Preserve physical G650 (RCU no-ref OFF). S0 PASS; S1 ACTIVE; S2 BLOCKED;
S3 BLOCKED; S4/S5/S6 BLOCKED; S7 PARTIAL. Do not widen the eligible
memory zones or bypass the writable-mapping guard based on this result.
No kernel production code, Android image, firmware or tablet state changed.
