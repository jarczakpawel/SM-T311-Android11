SM-T311 G663: old folio address to new object provenance
=========================================================

Scope and source correspondence
-------------------------------
This is a bounded sequentially consistent event explorer, not herd7,
ARMv7 weak-memory verification, XArray internals proof, or live test.
It enumerates all 6435 order-preserving merges of eight reader events
and seven writer events. Both streams are intentionally simplified.

Reader: rcu_read_lock -> xas_load -> PG_rcu_free -> i_size_read ->
xas_reload -> mapping/ZONE_NORMAL eligibility -> filemap_read_rcu_copy
-> rcu_read_unlock. Source: mm/filemap.c.

Writer: page_cache_delete (XA erase and mapping NULL) -> final put ->
new allocation at previously freed address, when available ->
XA publication -> marker PG_rcu_free under reference -> XA erase
and mapping NULL -> last put. Source: mm/filemap.c, mm/page_alloc.c.

Model A is the initial folio and model B is a distinct allocation
generation; A and B may have the same numeric page address. The
generation is ONLY a test oracle, not a Linux page field. Writer XArray
operations are modeled as single program-ordered actions; their spinlocks
and real XArray node lifetime are not simulated. Size is held at 4096
and the index at zero; all entries are order-0 Normal memory.

Results from tools/testing/selftests/smt311/rcu_provenance_g663.py
-----------------------------------------------------------------
With the old folio initially unmarked and tagged last-free deferral on:

* 6435 event-order merges, 72 accepted copies.
* Six copies where the numeric pointer obtained at the first xas_load()
  belongs to allocation A, but at the copy step belongs to new B.
* Zero copies from a dead folio and zero foreign-mapping copies in THIS
  deliberately constrained model. In the six mixed-generation examples,
  the flag check sees B marked, and B remains alive until reader unlock.
* Zero different-generation pairs between successful flag check and copy.

With the original donor already marked, there are no accepted
mixed-generation copies in the enumerated schedules, because its last
free cannot recycle A's address while the RCU reader is active.

Deliberate counterfactual source constraints:

* Disable deferred freeing for tagged B: 12 accepted reads of a
  no-longer-live object. This demonstrates why last-free RCU grace is
  essential; it does NOT claim current Linux behaves this way.
* Permit a deliberately inconsistent foreign B mapping in the slot:
  with the mapping check removed, 36 foreign-mapping reads are accepted.
  With the check present, none are accepted. Foreign B mapping in A's
  mapping slot is not a claimed reachable normal writer operation.
* Omitting xas_reload() by itself did not increase accepted copies in
  this narrow single-writer SC exploration: the mapping and lifetime
  conditions happened to exclude additional histories. This is NOT
  evidence that the reload is redundant under ARMv7 or in XArray itself.

Illustrative mixed-generation, live copy schedule::

    R_lock R_load
    W_erase_old W_old_last_put W_allocate_new
    W_publish_new W_mark_new_under_ref
    R_flag R_size R_reload R_mapping
    W_erase_new W_new_last_put
    R_copy R_unlock

In this schedule, the final put of marked B is deferred while R is
inside its RCU critical section. R can copy live B despite having
initially read A's address. That fact alone does not establish a
filesystem data race, stale EOF leak, or use-after-free.

Safety verdict
--------------
The SC exercise separates pointer ABA from actual object-lifetime
failure, and makes three assumptions visible: the flag is set under a
valid reference, every marked last-free waits for real RCU grace, and
the reader observes a coherently published marker. It does not establish
any of them for all callpaths or under actual ARMv7 weak memory.

S0 PASS; S1 ACTIVE; S2 BLOCKED; S3 BLOCKED; S4 BLOCKED; S5 BLOCKED;
S6 BLOCKED; S7 PARTIAL. No changes to mm/fs/arch production runtime;
physical G650 continues no-ref RCU OFF. Do not flash or use this model
as an authorization to enable the experimental path.
