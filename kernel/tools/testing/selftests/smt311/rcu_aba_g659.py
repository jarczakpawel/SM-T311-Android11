#!/usr/bin/env python3
import json
import re
import shutil
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parents[4]
model = root / "tools/memory-model"
expected = {
    "XALockUnmarkedABA": (2, 49),
    "XALockGraceABA": (1, 27),
    "XALockOldNewGenNoGrace": (2, 76),
    "XALockOldNewGenGrace": (1, 41),
}
herd = shutil.which("herd7")
if not herd:
    raise SystemExit("herd7 not available: S2 BLOCKED")

results = {}
for name, counts in expected.items():
    full = "SMT311+" + name
    proc = subprocess.run(
        [herd, "-I", str(model), "-conf", str(model / "linux-kernel.cfg"),
         str(model / "litmus-tests" / (full + ".litmus"))],
        capture_output=True, text=True, timeout=40
    )
    if proc.returncode:
        raise SystemExit(f"{full}: herd7 failed: {proc.stderr}")
    match = re.search(r"^Observation " + re.escape(full) + r" Sometimes (\d+) (\d+)$",
                      proc.stdout, re.M)
    if not match or tuple(map(int, match.groups())) != counts:
        raise SystemExit(f"{full}: observation differs from G659 pinned result")
    results[full] = {"observation": "Sometimes", "positive": counts[0], "negative": counts[1]}

source = (root / "mm/filemap.c").read_text()
xarray = (root / "include/linux/xarray.h").read_text()
page_alloc = (root / "mm/page_alloc.c").read_text()
checks = {
    "readside_lockless_xas_load": "folio = xas_load(xas);" in source,
    "flag_check_before_slot_reload": source.index("READ_ONCE(folio->flags.f) & __PG_RCU_FREE") < source.index("xas_reload(xas) != folio"),
    "reread_slot_checks_pointer_only": "if (xas_reload(xas) != folio)" in source,
    "slot_reload_reads_current_slot": "return xa_entry(xas->xa, node, offset);" in xarray,
    "writer_replacement_uses_xa_lock": "xas_lock_irq(&xas);\n\txas_store(&xas, new);" in source,
    "tagged_free_uses_rcu_defer": "rcu_free_defer(folio);" in page_alloc,
    # Since G680/G685 the pre-copy RCU section includes the runtime key
    # recheck and writable-mapping check. Order matters, not adjacency.
    "reader_has_rcu_lock": (
        lambda batch: (
            batch.index("rcu_read_lock();") <
            batch.index("pagefault_disable();") <
            batch.index("filemap_read_rcu_lookup(") and
            batch.rfind("rcu_read_unlock();") >
            batch.index("pagefault_enable();")
        )
    )(source.split("static bool filemap_read_rcu_batch(",1)[1].split(
         "size_t filemap_read_rcu(",1)[0]),
}
if not all(checks.values()):
    raise SystemExit("G659 source anchors changed; audit required")
result = {
    "label": "G659 diagnostic, NOT lifetime proof",
    "models": results,
    "source_checks": checks,
    "limitations": [
        "Slot value 1 abstracts a reused pointer; allocator, XArray internals and folio allocation identity are not modeled.",
        "An allowed observation is not proof of UAF or corrupted data.",
        "synchronize_rcu() results must not be interpreted as failure of real RCU.",
        "S1 ACTIVE; S2/S3 BLOCKED; RCU no-ref OFF."
    ],
}
(root / "Documentation/mm/sm-t311-rcu-aba-g659.json").write_text(json.dumps(result, indent=2) + "\n")
print(json.dumps(result, indent=2))
