SM-T311 G691: raw XArray pointer from initially UNTAGGED folio — ABA witness
=============================================================================

What the source actually checks
-------------------------------
filemap_read_rcu_lookup() in mm/filemap.c obtains a raw folio pointer
from xas_load() under RCU, then checks READ_ONCE(folio->flags.f)
& __PG_RCU_FREE, rereads i_size, executes smp_rmb() and xas_reload(),
compares folio pointer identity, checks folio_order/ZONE_NORMAL and
that folio->mapping matches the inode's address_space.

The allocator path mm/page_alloc.c:__free_pages_prepare() defers
physical recycling through rcu_free_defer() only if the folio had
PG_rcu_free set at its final put. If untagged, it takes the ordinary
free path. A tagged folio has its tag cleared during preparation,
then is queued on per-CPU pending/batch RCU lists, and becomes
reusable only after queue_rcu_work's GP or a draining explicit
synchronize_rcu_expedited(). The SM-T311 branch deliberately retains
the tag on inode exit, unlike upstream's normal elision.

filemap_mark_hot() may mark a NEW order-0 normal-zone EXT4 folio
at the SAME inode and index, if enabled and under appropriate
reference and memory pressure checks. Neither the raw folio pointer
nor XArray pointer equality carries an independent generation ID.

Relationship to G659 — NOT a new discovery of this risk
--------------------------------------------------------
G659 already found and modeled the "old unmarked folio, same address
reused" ambiguity, using FOUR real herd7 LKMM tests in
Documentation/mm/sm-t311-rcu-aba-g659.{rst,json}:

* XALockUnmarkedABA: Sometimes 2 / 49
* XALockGraceABA: Sometimes 1 / 27
* XALockOldNewGenNoGrace: Sometimes 2 / 76
* XALockOldNewGenGrace: Sometimes 1 / 41

Those numeric results are from DIFFERENT synthetic models, not a
reproduced real memory bug, and the allowed grace observations do NOT
prove RCU failure. G691 specifically extends G659 by connecting the
reader's raw-pointer checks to the actual conditional last-put
allocator RCU queue, same inode/index page reuse and NEW-generation
PG_rcu_free marking. The G691 finite-state "tagged original: no
witness" result applies only to its deliberately simplified model.
It does NOT supersede, disprove or re-interpret any G659 LKMM
outcome. Keeping S2 BLOCKED is essential.

G691 bounded conceptual interleavings
--------------------------------------
tools/testing/selftests/smt311/rcu_untagged_reuse_aba_g691.py
verifies 19 source conditions and rejects 9 real changed-source
inputs. It then explores two tiny explicitly described schedules
with a speculative reader and single writer:

  R_LOAD:   read physical struct page pointer P at mapping[index]
  W_REMOVE: remove the old folio, mapping[index] becomes absent
  W_LAST_PUT: old UNTAGGED folio is immediately reusable (not GP queued)
  W_REUSE:  allocator reuses physical address P for a new generation
  W_INSERT: install new generation at SAME mapping[index], pointer P
  W_MARK:   mark new folio PG_rcu_free while it has a reference
  R_FLAG:   old reader reads NEW generation's PG_rcu_free = true
  R_SIZE:   inode i_size permits position
  R_RELOAD: XArray slot holds P again; raw pointer equality succeeds
  R_MAP:    new folio mapping is SAME address_space; comparison succeeds
  R_COPY:   reader can accept new incarnation in the conceptual model

Exact finite-state exploratory results:
- Initially UNTAGGED folio: 54 abstract states, an acceptance witness.
- Initially TAGGED folio: 47 states; with reuse requiring a grace
  period, no acceptance witness for reusing same P during reader RCU.
- Hypothetical atomic generation identity validation: 54 states,
  no same-P acceptance witness because generation changed.

All these are SOURCE-ANCHORED ABSTRACT MODELS, not a herd7 result,
not an actual ARMv7 KCSAN trace, and not a demonstrated use-after-free.
The model assumes same inode, index, physical struct page address,
and that the second occupant can become hot/marked before the
reader's flag lookup. Real reclaim, memory ordering, rcu scheduler
and data consistency constraints are not fully represented.

Implication
-----------
The existing flag -> xas_reload -> mapping triple-check CANNOT be
claimed to prove the *absence of all old-untagged -> new-tagged
same-physical-address ABA schedules*. RCU grace protection on the
second occupant does not retroactively protect the original, freely
reusable folio from which the stale pointer was first obtained.

Do not make an unsafe narrow fix such as adding one more plain
READ_ONCE(mapping) or smp_rmb(): neither supplies a generation
identity or protection before the first unpinned dereference.

Possible research directions, NOT implemented:
- Generation identity coupled atomically to XArray slot and checked
  safely without dereferencing a recycled struct page. A stand-alone
  page->generation field is not sufficient if it is itself reused.
- Defer physical recycling of ALL potentially sampled EXT4 filecache
  folios during the optional experiment, regardless of marking.
  Would risk large Normal-zone memory pressure and costly grace waits
  on the dual Cortex-A9 device; do not rush it in.
- Acquire a safe ref before consulting the candidate folio and use
  the ordinary refcounted fallback, foregoing unreferenced speedup.
- Keep the normal RCU-OFF production profile until genuine
  lifetime/ABA/cache/lockdep and benchmark gates are demonstrated.

Nothing in G691 authorizes RCU ON on the physical tablet. S2 remains
BLOCKED, S3 remains BLOCKED. G691 modifies only diagnostics and
documentation, no executable C. FOUR previously staged runtime
source paths (mm/filemap.c, mm/page_alloc.c, mm/shmem.c, mm/vma.c)
remain NOT INSTALLED versus physical G650 kernel source a2fca4558.

No BOOT pack/flash/TWRP, no backups, audio/camera/phone/PIN/OTG.
S0 PASS S1 ACTIVE S2 BLOCKED S3 BLOCKED S4 BLOCKED S5 BLOCKED
S6 BLOCKED S7 PARTIAL S8 ACTIVE.
