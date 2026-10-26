SM-T311 G676: guarantee rcu_work completion when draining deferred folios
==========================================================================

Problem and exact kernel API contract
-------------------------------------
mm/page_alloc.c:rcu_free_sync() previously used::

    flush_work(&r->rwork.work);

The embedded work_struct belongs to an rcu_work queued with
queue_rcu_work(). Its execution is deliberately delayed until an RCU
grace period has passed; the callback invokes __queue_work() only
then. queue_rcu_work() sets the WORK_STRUCT_PENDING_BIT *before*
installing the RCU callback via call_rcu_hurry(). A pending rcu_work
can therefore be registered with RCU yet NOT queued in the ordinary
workqueue. Plain flush_work() has no API guarantee that this preceding
RCU callback has completed.

kernel/workqueue.c:flush_rcu_work() specifically checks pending and
runs rcu_barrier() before flush_work() if needed. It explicitly covers
the interval between RCU callback registration and ordinary workqueue
submission. The same API is used in mm/slab_common.c for deferred work.

The change
----------
rcu_free_sync() uses::

    flush_rcu_work(&r->rwork);
    flush_work(&r->expedite);

The separate normal work for expedite remains unchanged.
This is correct for both the default RCU free implementation and
CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT; no extra data structure or
alloc/free hotpath locking is introduced. This path is used by memory
hotplug, memory failure and allocator drain/reclaim. It is a rare
synchronization path, not the filemap_read() fastpath.

rcu_free_drain() first removes current pending and batch entries under
per-CPU locks and waits for a grace period before freeing the detached
entries. The additional flush_rcu_work() covers the distinct in-flight
callback/workqueue completion contract. The code does not promise
that new concurrent frees cannot enter queues AFTER a sync invocation.

Possible performance cost
-------------------------
If the RCU work is pending but not yet submitted, flush_rcu_work()
may wait through rcu_barrier() (global RCU callback barrier). That
can increase latency in rare memory-hotplug, memory-failure or
allocation-synchronization paths. It is deliberately NOT added to
the normal read/write or the common folio-free path.

Validation
----------
Host-only source test::

    python3 tools/testing/selftests/smt311/rcu_work_completion_g676.py

16/16 source checks PASS and five actual modified-source negative
variants REJECT. Tiny callback-state illustration distinguishes a
registered RCU callback from an ordinary enqueued work item. This is
NOT a kernel threading reproduction and does not establish an
observable physical deadlock in the old implementation.

An isolated real ARM32 build with the SM-T311 defconfig succeeded::

    make -j2 O=../build/g674-inode-aba-safety ARCH=arm \
      CROSS_COMPILE=arm-linux-gnueabihf- mm/page_alloc.o

The result is an ARM EABI5 relocatable object. Its SHA256:
10db37874994841e88c7da49f870d234a7a9d946b9cce03cabf02a89204fd63f

Older G674 ABA, G675 reclaim and S0-S8 plan tests also passed.
The new binary is not a signed BOOT image.

Limits / deployment
-------------------
The two executable differences relative to the physical G650 release
are now:

- mm/filemap.c — G674 inode-exit PG_rcu_free preservation.
- mm/page_alloc.c — G676 specialized rcu_work completion wait.

Both remain STAGED NOT INSTALLED. Device still boots G650; no RCU ON,
no new flash, no backups and no audio/camera/phone tests.

S0 PASS, S1 ACTIVE, S2 BLOCKED, S3 BLOCKED, S4 BLOCKED,
S5 BLOCKED, S6 BLOCKED, S7 PARTIAL, S8 ACTIVE. This correction
addresses one workqueue completion precondition, NOT all folio
lifetime ABA paths, HighMem/VIPT coherence, or measured Cortex-A9
performance. No runtime activation until S2/S3 actually PASS.
