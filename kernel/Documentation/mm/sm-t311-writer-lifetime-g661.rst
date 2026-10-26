SM-T311 G661: source-level folio writer/read-side lifetime graph
================================================================

Purpose
-------
Close concrete gaps in the G659/G660 source inventory without changing
production runtime. All observations below are from the local kernel tree.
The 24 machine-checked anchors are in
tools/testing/selftests/smt311/rcu_writer_lifetime_g661.py and
Documentation/mm/sm-t311-writer-lifetime-g661.json.

Reader
------
filemap_read_rcu_lookup() acquires its first folio address from xas_load()
without taking xa_lock. The speculative read first tests PG_rcu_free,
then reads i_size, then checks the current XArray slot with xas_reload(),
then checks folio order, zone and mapping. The mapping and XArray
comparisons use pointers, not allocator generation identities. The
32-bit SMP i_size accessor uses a seqcount retry.

Writers and lifetime
--------------------
1. page_cache_delete() executes with the XArray lock held by its caller;
   it replaces the slot before nulling folio->mapping. If the inode is
   exiting, it clears the deferred-free tag under the assumption that
   no ref-free reader can still access the evicted inode.
2. page_cache_delete_batch() clears a folio tag for an exiting inode
   and clears the XArray slot. Its internal order differs from the
   single-delete path: folio->mapping becomes NULL before xas_store().
   This is why reader ordering must not be inferred from only one path.
3. replace_page_cache_folio() stores the new pointer with xa_lock_irq()
   held, then nulls old->mapping. It puts the old folio after unlocking.
4. __filemap_add_folio() checks conflicting XArray entries while locked
   before publishing the new folio pointer.
5. __folio_migrate_mapping() freezes references under an XA/swap lock,
   replaces the mapping slot, then unfreezes the old folio reference
   count. folio_migrate_flags() does not propagate PG_rcu_free to the
   destination. Source lifetime still depends on the old folio's tag
   and final put. This does not prove every filesystem migration path.
6. truncate_inode_pages_final() sets AS_EXITING, cycles the XArray
   lock and then truncates the mapping. clear_inode() destroys
   the mapping XArray after page-cache eviction.
7. __free_pages_prepare() clears PG_rcu_free and diverts the last free
   into rcu_free_defer(); its RCU workqueue and expedited flush routes
   release tagged pages after their corresponding grace periods.
8. Huge folio split copies __PG_RCU_FREE into new heads before
   clear_compound_head(), with smp_wmb() before the tail transition.
9. Memory-failure release/unmap special paths wait for synchronize_rcu()
   for tagged folios.
10. The experimental hot marker checks zone, uptodate state, mapping
    and other eligibility requirements before setting PG_rcu_free.
    This marking is under an existing reference on the normal read path;
    the machine check does not prove all possible callers uphold this.

Unresolved ABA obligation
-------------------------
The writer-side XA lock does not serialize the speculative xas_load()
itself. An initially *unmarked* donor folio could reach last put without
deferred freeing, and its page metadata could be reused at the same
address before the reader inspects PG_rcu_free. If a new marked folio
is published at the same pointer and index, the pointer comparison
alone is not an object-generation comparison. It remains to establish
whether the real ARMv7 memory-ordering and allocation constraints allow
this to produce incorrect user data or an unsafe dereference. No
confirmed kernel UAF or corruption is claimed.

G661 checks are not a formal exhaustive callgraph or allocator proof.
Three deliberately changed source anchors are rejected by the focused
negative checks, but these checks do not exercise real memory races.

Decision and implementation
---------------------------
No runtime fix is justified yet without a demonstrated unsafe execution
trace and regression acceptance. Keep the G650 physical BOOT and no-ref
RCU OFF. S0 PASS, S1 ACTIVE, S2 BLOCKED, S3 BLOCKED, S4 BLOCKED,
S5 BLOCKED, S6 BLOCKED, S7 PARTIAL. Further review must distinguish
the old unmarked donor's lifetime from the new marked folio's lifetime,
and check potential copying while the new folio is being replaced.
Do not run an unsafe RCU ON experiment or flash docs-only changes.
