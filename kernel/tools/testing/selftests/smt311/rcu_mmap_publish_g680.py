#!/usr/bin/env python3
"""G680 source-bound writable-mmap publication RCU handshake regression.

Actual herd7 v7.58 LKMM test of an abstract writable-count publication.
Includes real kernel-source anchors and changed-C-source negative controls.
The LKMM model is not physical proof of Cortex-A9 cache coherence.
"""
import hashlib
import json
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
OUT = ROOT / "Documentation/mm/sm-t311-rcu-mmap-publish-g680.json"
TESTS = {
    "with_gp": ("SMT311+WritableMmapGraceG680", "Never", 0, 3),
    "no_gp": ("SMT311+WritableMmapNoGraceG680", "Sometimes", 1, 3),
    "barrier_only": ("SMT311+WritableMmapBarrierNotGraceG680", "Sometimes", 1, 3),
    "precheck_before_rcu": ("SMT311+WritableMmapCheckBeforeLockG680", "Sometimes", 1, 3),
}
PATHS = ("mm/filemap.c", "mm/vma.c", "include/linux/fs.h",
         "include/linux/pagemap.h", "arch/arm/mm/flush.c")
SRC = {k:(ROOT/k).read_text() for k in PATHS}


def sub(source, start, end):
    assert start in source, start
    tail = source.split(start,1)[1]
    assert end in tail, end
    return tail.split(end,1)[0]


def in_order(source, *tokens):
    pos=[source.find(x) for x in tokens]
    return all(n >= 0 for n in pos) and pos == sorted(pos) and len(set(pos)) == len(pos)


def gates(all_src):
    fm,vma,fs,pg,arm=(all_src[p] for p in PATHS)
    read=sub(fm,"static bool filemap_read_rcu_batch(", "size_t filemap_read_rcu(")
    mmap=sub(vma, "unsigned long mmap_region(", "/**\n * do_brk_flags(")
    writable=sub(fs, "static inline int mapping_writably_mapped(", "static inline int mapping_deny_writable(")
    mark=sub(fm, "static noinline void filemap_mark_hot(", "void filemap_read_accessed(")
    return {
        "rcu_reader_enters_before_mapping_test":
            in_order(read,"rcu_read_lock();",
                     "mapping_writably_mapped(mapping)", "pagefault_disable();"),
        "reader_mmap_failure_unwinds_rcu":
            in_order(read.split("if (ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE &&",1)[1],
                     "mapping_writably_mapped(mapping)", "rcu_read_unlock();",
                     "return false;", "pagefault_disable();"),
        "reader_copy_not_before_mmap_check":
            in_order(read,"mapping_writably_mapped(mapping)",
                     "filemap_read_rcu_lookup(", "filemap_read_rcu_copy("),
        "reader_disables_user_faults_during_copy":
            in_order(read,"pagefault_disable();","filemap_read_rcu_copy(",
                     "pagefault_enable();") and
            read.rfind("rcu_read_unlock();") > read.find("pagefault_enable();"),
        "mmap_increments_writable_count_first":
            in_order(mmap, "mapping_map_writable(file->f_mapping)",
                     "synchronize_rcu();","__mmap_region("),
        "mmap_sync_gated_to_SM_T311_experiment":
            "#ifdef CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT" in mmap,
        "mmap_sync_only_if_eligible_mapping":
            "file->f_mapping->host->i_sb->s_magic == EXT4_SUPER_MAGIC" in mmap and
            "mapping_rcu_read(file->f_mapping)" not in mmap,
        "mmap_unwinds_temp_counter":
            "mapping_unmap_writable(file->f_mapping);" in mmap,
        "kernel_counter_positive_for_existing_shared_mmap":
            "atomic_read(&mapping->i_mmap_writable) > 0" in writable,
        "kernel_writable_map_publishes_atomic_count":
            "atomic_inc_unless_negative(&mapping->i_mmap_writable)" in writable,
        "persistent_RCU_eligibility_bit":
            "test_bit(AS_RCU_READ, &mapping->flags)" in pg and
            "set_bit(AS_RCU_READ, &mapping->flags)" in pg and
            "mapping_set_rcu_read(mapping);" in fm,
        "ARM32_arch_needs_explicit_flush":
            "void flush_dcache_folio(struct folio *folio)" in arm and
            "ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE" in read,
        "slow_path_flushes_writable_alias":
            "if (writably_mapped)" in fm and
            "flush_dcache_folio(folio);" in fm,
        "mmap_wait_not_read_fastpath":
            "synchronize_rcu();" not in read and
            "synchronize_rcu();" in mmap,
        "marker_disallows_nonext4_nonorder0":
            "EXT4_SUPER_MAGIC" in mark and "folio_test_large(folio)" in mark,
    }


def mutated_sources(source):
    attempts={
        "reader_mmap_check_before_rcu": ("mm/filemap.c",
            "rcu_read_lock();\n#ifdef CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT",
            "#ifdef CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT"),
        "reader_failure_missing_rcu_unlock": ("mm/filemap.c",
            "mapping_writably_mapped(mapping)) {\n\t\trcu_read_unlock();",
            "mapping_writably_mapped(mapping)) {"),
        "mmap_barrier_instead_of_gp": ("mm/vma.c",
            "if (file->f_mapping->host &&\n\t\t    file->f_mapping->host->i_sb->s_magic == EXT4_SUPER_MAGIC)\n\t\t\tsynchronize_rcu();",
            "if (file->f_mapping->host &&\n\t\t    file->f_mapping->host->i_sb->s_magic == EXT4_SUPER_MAGIC)\n\t\t\tsmp_mb();"),
        "mmap_no_gp": ("mm/vma.c",
            "if (file->f_mapping->host &&\n\t\t    file->f_mapping->host->i_sb->s_magic == EXT4_SUPER_MAGIC)\n\t\t\tsynchronize_rcu();",
            "if (file->f_mapping->host &&\n\t\t    file->f_mapping->host->i_sb->s_magic == EXT4_SUPER_MAGIC)\n\t\t\t;"),
        "mmap_missing_eligibility_predicate": ("mm/vma.c",
            "file->f_mapping->host->i_sb->s_magic == EXT4_SUPER_MAGIC",
            "file->f_mapping->host->i_sb->s_magic != EXT4_SUPER_MAGIC"),
        "arch_mmap_flush_absent": ("mm/filemap.c",
            "mapping_writably_mapped(mapping)) {",
            "0 /* mapping check removed */) {"),
        "rcu_candidate_flag_not_persistent": ("include/linux/pagemap.h",
            "set_bit(AS_RCU_READ, &mapping->flags);",
            "/* AS_RCU_READ store removed */"),
    }
    rejected={}
    for name,(file,old,new) in attempts.items():
        assert old in source[file],(name,old)
        changed=dict(source);changed[file]=changed[file].replace(old,new,1)
        failed=[g for g,v in gates(changed).items() if not v]
        assert failed, f"Actual source mutation escaped: {name}"
        rejected[name]=failed
    return rejected


def herd_runs():
    result={}
    for alias,(test,expected,pos,neg) in TESTS.items():
        path=ROOT/"tools/memory-model/litmus-tests"/(test+".litmus")
        raw=path.read_text()
        assert raw.startswith("C "+test+"\n")
        assert raw.count("rcu_read_lock();")== (0 if alias == "precheck_before_rcu" else 1) or (
            alias=="precheck_before_rcu" and raw.count("rcu_read_lock();")==1)
        if alias=="with_gp" or alias=="precheck_before_rcu":
            assert "synchronize_rcu();" in raw
        else:
            assert "synchronize_rcu();" not in raw
        p=subprocess.run(["herd7","-I","tools/memory-model","-conf",
                        "tools/memory-model/linux-kernel.cfg",
                        str(path.relative_to(ROOT))],cwd=ROOT,
                        capture_output=True,text=True,check=True,timeout=35)
        m=re.search(r"(?m)^Observation (\S+) (Never|Sometimes|Always) (\d+) (\d+)\s*$",p.stdout)
        assert m and (m.group(1),m.group(2),int(m.group(3)),int(m.group(4)))==(
            test,expected,pos,neg),(test,p.stdout[-1000:])
        normalized=re.sub(r"(?m)^Time [^\n]*\n?","",p.stdout)
        result[alias]={"test":test,"outcome":expected,"positive":pos,
                       "negative":neg,"litmus_sha256":hashlib.sha256(raw.encode()).hexdigest(),
                       "herd_normalized_stdout_sha256":hashlib.sha256(normalized.encode()).hexdigest(),
                       "observation":m.group(0)}
    return result


def main():
    checks=gates(SRC)
    missing=[key for key,value in checks.items() if not value]
    assert len(checks)==15
    if missing:
        raise AssertionError("source gate failure "+repr(missing))
    rejected=mutated_sources(SRC)
    herd=herd_runs()
    assert len(rejected)==7
    assert herd["with_gp"]["positive"]==0
    assert all(herd[k]["positive"]>0 for k in ("no_gp","barrier_only","precheck_before_rcu"))
    info={
      "stage":"G680", "type":"real Linux LKMM herd7 + bounded C source gates",
      "source_checks":checks,"source_checks_pass":len(checks),
      "mutated_source_rejections":rejected,"herd7_models":herd,
      "staged_runtime_files":["mm/filemap.c","mm/page_alloc.c","mm/vma.c"],
      "performance_tradeoff":"SM-T311 shared writable mmap of RCU-tagged inode waits a synchronize_rcu() before VMA publication. Extra latency must be measured, particularly while mmap_lock is held.",
      "limitations":[
        "Model scalar count and publication flag, not actual VMAs, ARMv7 cache or page allocator.",
        "RCU GP prevents only the modeled pre-check/post-publication race; no general PFN ABA safety proof.",
        "mmap VMA clone/remap paths and other writable aliases need separate manual review.",
        "No ARMv7 runtime ON safety proven; all staged executable files remain NOT INSTALLED.",
        "S1 ACTIVE, S2/S3 BLOCKED, RCU default OFF."
      ]}
    OUT.write_text(json.dumps(info,indent=2,ensure_ascii=False)+"\n")
    print(f"G680 herd7: {[(k,x['outcome'],x['positive']) for k,x in herd.items()]}")
    print(f"G680 source 15/15 PASS; {len(rejected)}/7 changed-source mutations REJECTED")

if __name__=="__main__":
    main()
