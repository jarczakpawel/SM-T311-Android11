#!/usr/bin/env python3
"""G678: run REAL herd7 Linux-Kernel-Memory-Model cross-inode ABA diagnostics.

Four C litmus files distinguish actual RCU grace from an ordinary
full memory barrier and from an absent reader RCU critical section.
Uses the in-tree linux-kernel.cfg/cat and a real herdtools7 executable.
Oracle 'generation' is TEST-ONLY; outcomes do not establish a live UAF
or provide full proof for the real kernel/ARMv7 allocator.
"""
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[4]
LITMUS = ROOT / "tools/memory-model/litmus-tests"
CFG = ROOT / "tools/memory-model/linux-kernel.cfg"
OUT = ROOT / "Documentation/mm/sm-t311-rcu-cross-inode-lkmm-g678.json"

FILES = {
    "without_grace": ("SMT311+CrossInodeRecycledBeforeGraceG678", "Sometimes", 6, 318),
    "with_grace": ("SMT311+CrossInodeRecycledAfterGraceG678", "Never", 0, 71),
    "barrier_not_grace": ("SMT311+CrossInodeBarrierNotGraceG678", "Sometimes", 4, 233),
    "reader_without_rcu": ("SMT311+CrossInodeReaderNoRCUG678", "Sometimes", 4, 233),
}

def sha(x):
    return hashlib.sha256(x.encode()).hexdigest()

def source_checks():
    fm = (ROOT / "mm/filemap.c").read_text()
    alloc = (ROOT / "mm/page_alloc.c").read_text()
    work = (ROOT / "kernel/workqueue.c").read_text()
    look = fm.split("static struct folio *filemap_read_rcu_lookup(", 1)[1].split(
        "static size_t filemap_read_rcu_size(", 1)[0]
    batch = fm.split("static bool filemap_read_rcu_batch(", 1)[1].split(
        "size_t filemap_read_rcu(", 1)[0]
    single = fm.split("static void page_cache_delete(", 1)[1].split(
        "static void filemap_unaccount_folio(", 1)[0]
    multi = fm.split("static void page_cache_delete_batch(", 1)[1].split(
        "void delete_from_page_cache_batch(", 1)[0]
    free = alloc.split("static __always_inline bool __free_pages_prepare(", 1)[1].split(
        "trace_mm_page_free(page, order);", 1)[0]
    result = {
        "reader_rcu_critical_section": (
            "rcu_read_lock();" in batch and "rcu_read_unlock();" in batch),
        "reader_first_xas_load": (
            "folio = xas_load(xas);" in look),
        "reader_flag_check": (
            "READ_ONCE(folio->flags.f) & __PG_RCU_FREE" in look),
        "reader_order_flag_before_reload": (
            look.index("READ_ONCE(folio->flags.f)") < look.index("xas_reload(xas) != folio")),
        "reader_includes_barriers": (
            look.count("smp_rmb();") >= 2),
        "reader_checks_file_mapping": (
            "READ_ONCE(folio->mapping) != mapping" in look),
        "source_no_generation_comparison": (
            "folio->generation" not in look and "folio_generation" not in look),
        "G674_single_inode_exit_keeps_tag": (
            "!IS_ENABLED(CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT) &&" in single),
        "G674_batch_inode_exit_keeps_tag": (
            "!IS_ENABLED(CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT) &&" in multi),
        "allocator_clears_flag_on_final_put": (
            "__folio_clear_rcu_free(folio);" in free),
        "allocator_defers_reuse_when_tagged": (
            "rcu_free_defer(folio);" in free),
        "rcu_worker_uses_queued_grace": (
            "queue_rcu_work(rcu_free_wq, &r->rwork)" in alloc and
            "call_rcu_hurry(&rwork->rcu, rcu_work_rcufn)" in work),
    }
    assert len(result) == 12
    missing = [k for k, v in result.items() if not v]
    if missing:
        raise AssertionError("Source no longer matches G678 model: " + repr(missing))
    return result

def runner():
    version = subprocess.run(["herd7", "-version"], text=True, capture_output=True,
                             timeout=12, check=True)
    assert "7.58" in version.stdout + version.stderr, "Check expected herd7 version"
    assert CFG.is_file() and (ROOT/"tools/memory-model/linux-kernel.cat").is_file()
    results={}
    for key,(name,expected,positive,negative) in FILES.items():
        path=LITMUS/(name+".litmus")
        content=path.read_text()
        assert content.startswith("C "+name+"\n")
        assert "exists (0:r0=1" in content
        assert content.count("spin_lock(xa_lock);") == 2
        assert content.count("spin_unlock(xa_lock);") == 2
        if key in ("with_grace","reader_without_rcu"):
            assert content.count("synchronize_rcu();")==1
        else:
            assert "synchronize_rcu();" not in content
        if key=="reader_without_rcu":
            assert "rcu_read_lock();" not in content
        elif key != "reader_without_rcu":
            assert "rcu_read_lock();" in content
        if key=="barrier_not_grace":
            assert content.count("smp_mb();")==1

        completed=subprocess.run(
            ["herd7","-I","tools/memory-model","-conf",
             "tools/memory-model/linux-kernel.cfg",str(path.relative_to(ROOT))],
            cwd=ROOT, text=True, capture_output=True, timeout=50
        )
        if completed.returncode:
            raise AssertionError(f"herd7 FAILED on {name}: {completed.stderr[-1400:]}")
        output=completed.stdout
        # herd7 includes its wall-clock elapsed time in the stdout footer.
        # Normalize ONLY that volatile line to keep committed evidence
        # reproducible; all observations, counts and model hashes remain.
        normalized_output=re.sub(r"(?m)^Time [^\n]*\n?", "", output)
        observations=re.findall(
            r"(?m)^Observation\s+(\S+)\s+(Never|Sometimes|Always)\s+(\d+)\s+(\d+)\s*$",
            output)
        assert len(observations)==1,(name,output[-1400:])
        model,outcome,pos,neg=observations[0]
        assert model==name and outcome==expected and int(pos)==positive and int(neg)==negative, (
            name, observations)
        assert "Witnesses" in output and "Condition exists" in output
        results[key]={
            "name":name,
            "model_file":str(path.relative_to(ROOT)),
            "model_sha256":sha(content),
            "outcome":outcome,"positive_witnesses":positive,
            "negative_witnesses":negative,
            "herd7_normalized_stdout_sha256":sha(normalized_output),
            "herd7_observation":f"Observation {model} {outcome} {pos} {neg}",
            "herd7_outcome_footer":normalized_output[-300:].strip()
        }
    return version.stdout.strip() or version.stderr.strip(),results

def main():
    guards=source_checks()
    version,models=runner()
    assert models["without_grace"]["positive_witnesses"]>0
    assert models["with_grace"]["positive_witnesses"]==0
    assert models["barrier_not_grace"]["positive_witnesses"]>0
    assert models["reader_without_rcu"]["positive_witnesses"]>0
    report={
        "stage":"G678","tool":"real herdtools7 LKMM", "tool_version":version,
        "config":"tools/memory-model/linux-kernel.cfg",
        "models":models,
        "output_normalization":"Removed only volatile Time line from actual herd7 stdout before hashing and storing footer.",
        "source_gates":guards,"source_gates_passed":len(guards),
        "paired_negative_controls":[
            "Removing RCU grace allows the mixed-generation condition.",
            "Replacing grace with smp_mb also allows mixed-generation outcome.",
            "Removing the reader RCU read-side critical section allows mixed-generation outcome."
        ],
        "interpretation":"RCU GP + reader lock prevents this particular modeled cross-inode tag-generation observation under LKMM; full memory barrier does not substitute for GP.",
        "critical_limits":[
            "Abstract scalar slot=1 is a numeric same-address pointer, generation is a TEST oracle not present in kernel.",
            "Writer serializes inode A, B and C but does not model all XArray nodes, allocator reuse or RCU callback queue details.",
            "The oracle generation reads add ordering/constraints absent from the actual ref-free reader.",
            "LKMM is a portable kernel memory model, not a complete ARMv7 machine/cache/HIGHMEM execution proof.",
            "An allowed mixed-generation condition is NOT a live UAF; its absence is NOT proof whole algorithm is safe.",
            "The special G674 inode-exit tag preservation is STAGED NOT INSTALLED; physical kernel G650 remains default-OFF.",
            "S2/S3 remain BLOCKED; no-ref RCU ON is NOT approved."
        ]
    }
    OUT.write_text(json.dumps(report, indent=2,ensure_ascii=False)+"\n")
    print("G678 herd7",version)
    print("G678 LKMM",[(name, r["outcome"],r["positive_witnesses"],r["negative_witnesses"])
                       for name,r in models.items()])
    print("G678 source gates",len(guards),"/",len(guards),"PASS; S2/S3 BLOCKED")

if __name__=="__main__":
    main()
