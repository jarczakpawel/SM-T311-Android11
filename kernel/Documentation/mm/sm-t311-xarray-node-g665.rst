SM-T311 G665: XArray cached-node retirement and root-membership audit
=======================================================================

Issue
-----
A reference-free page-cache reader runs xas_load() under RCU, then
checks PG_rcu_free, i_size and xas_reload() on the same xa_state. The
saved xas->xa_node is protected from memory reclamation by RCU, but
being allocated does not imply still belonging to the XArray root.
The reader compares the reloaded entry with the original folio pointer.
This is not an allocation-generation comparison.

Real XArray source-path review
------------------------------
1. xas_set() moves xa_state to XAS_RESTART; xas_load() calls xas_start()
   and descends from xa_head. xas_reload() instead reuses saved xa_node.
2. In xas_shrink() lib/xarray.c, root is changed with RCU_INIT_POINTER.
   When promoting a non-node entry, the old root's slots[0] is changed
   to XA_RETRY_ENTRY before xa_node_free(). Thus a reader retaining
   that old root slot does not receive the stale previous page pointer
   in the inspected shrink branch.
3. xas_delete_node() handles count == 0 nodes. It retires the node and
   clears its parent slot. The final entry has already been removed in
   xas_store(). The cached slot in this branch should not equal a live
   old folio after the final erase.
4. xas_free_nodes() explicitly replaces surviving non-empty slots of
   an unlinked subtree with XA_RETRY_ENTRY before retiring the nodes.
5. xa_node_free() calls call_rcu() before actual node reclamation.
   This protects node memory during an active RCU read section. It does
   not by itself establish a valid CURRENT tree relationship.
6. Existing in-tree lib/test_xarray.c includes check_xas_retry() and
   check_xa_shrink(), including old-slot retry assertions.

Offline source gate
-------------------
tools/testing/selftests/smt311/rcu_xarray_node_g665.py checks 20
source properties and reruns the same source checks after five real
in-memory deletions of the key anchors (retired root retry marker,
subtree retry marker, call_rcu retirement, parent slot clear and reader
slot reload). All 20 structural conditions passed; all five negative
mutations were rejected. Results are retained as
Documentation/mm/sm-t311-xarray-node-g665.json.

Real userspace XArray regression
--------------------------------
Added tools/testing/radix-tree/smt311-xas-g665.c with a dedicated
Makefile target. It exercises the ACTUAL lib/xarray.c implementation via
the upstream userspace test harness, covering an RCU reader retaining a
slot across root shrink, subtree destroy and final erase.

Build command::

    make -C tools/testing/radix-tree smt311-xas-g665 -j2

When built, run::

    tools/testing/radix-tree/smt311-xas-g665

The binary test did NOT run in G665. The host failed to compile because
the existing upstream userspace harness requires liburcu development
headers and this host lacks both urcu.h and urcu/uatomic.h. The failed
build is not a kernel test failure and not a PASS. Installing the
normal distribution's liburcu development package on a machine that
supports the build is a prerequisite. Do not replace real liburcu
with a fake stub and claim the test passed.

Limits
------
This source review and pending C regression only inspect selected
XArray retirement branches. They are not complete state-space coverage,
an ARMv7 weak-memory proof, an allocator-generation correctness proof,
or runtime validation of a reference-free reader. In particular,
root membership, inode teardown and reallocation races across a
real concurrently running pair of CPUs still need proof; independently
of that, a newly allocated folio reusing an unmarked folio's address
remains the G663 provenance case.

Physical production image remains G650 unchanged. S0 PASS, S1 ACTIVE,
S2 BLOCKED, S3 BLOCKED, S4 BLOCKED, S5 BLOCKED, S6 BLOCKED, S7 PARTIAL.
Do not enable ref-free RCU or flash a docs/test-only kernel commit.
