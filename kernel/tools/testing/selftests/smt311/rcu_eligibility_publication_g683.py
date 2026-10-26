#!/usr/bin/env python3
"""G683: AS_RCU_READ eligibility vs mmap writable count publication.

G680's test_bit(AS_RCU_READ) guard for skipping mmap-side GP is not a
synchronization handshake with the independent writable-map counter.
Checks actual Linux source, real herd7 LKMM scenarios, and changed-source
negative tests. An LKMM witness is NOT proof of physical Cortex-A9 UAF.
"""
from pathlib import Path
import hashlib
import json
import re
import subprocess

ROOT=Path(__file__).resolve().parents[4]
OUT=ROOT/"Documentation/mm/sm-t311-rcu-eligibility-publication-g683.json"
PATHS=("mm/vma.c","mm/filemap.c","include/linux/pagemap.h",
       "include/linux/fs.h","kernel/rcu/tree.c")
SRC={x:(ROOT/x).read_text() for x in PATHS}
MODELS={
    "old_eligibility_guard_race":("SMT311+MmapEligiblePublicationRaceG683","Sometimes",1,8),
    "unconditional_ext4_grace":("SMT311+MmapEligibleAlwaysGraceG683","Never",0,8),
    "writer_barrier_is_not_grace":("SMT311+MmapEligibleRelaxedNoGPBarrierG683","Sometimes",1,8),
    "reader_barrier_counterfactual":("SMT311+MmapEligibleReaderBarrierG683","Never",0,7),
}


def section(text,start,end):
    assert start in text,start
    right=text.split(start,1)[1]
    assert end in right,end
    return right.split(end,1)[0]


def ordered(text,*terms):
    idx=[text.find(t) for t in terms]
    return all(n>=0 for n in idx) and idx==sorted(idx) and len(set(idx))==len(idx)


def checks(src):
    vma,fm,pg,fs,rcu=(src[k] for k in PATHS)
    writer=section(vma,"unsigned long mmap_region(","/**\n * do_brk_flags(")
    block=section(writer,"if (file && is_shared_maywrite(&vma_flags)) {",
                  "writable_file_mapping = true;")
    noncomment=re.sub(r"/\*.*?\*/","",block,flags=re.S)
    batch=section(fm,"static bool filemap_read_rcu_batch(", "size_t filemap_read_rcu(")
    dispatch=section(fm,"size_t filemap_read_rcu(", "ssize_t filemap_read(")
    mark=section(fm,"static noinline void filemap_mark_hot(", "void filemap_read_accessed(")
    return {
      "experiment_compile_only_guard":
        "#ifdef CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT" in writer,
      "ext4_super_magic_header_is_included":
        "#include <linux/magic.h>" in vma,
      "tag_marker_only_ext4_normal_4k":
        all(x in mark for x in ("EXT4_SUPER_MAGIC","ZONE_NORMAL",
                               "folio_test_large(folio)",
                               "folio_test_set_rcu_free(folio)")),
      "marker_publish_tag_then_eligibility":
        ordered(mark,"folio_test_set_rcu_free(folio)",
                "mapping_set_rcu_read(mapping);") and
        "if (READ_ONCE(folio->mapping) == mapping)\n\t\tmapping_set_rcu_read(mapping);" in mark,
      "eligibility_is_atomic_flag_independent_of_writable_counter":
        "set_bit(AS_RCU_READ, &mapping->flags)" in pg and
        "return test_bit(AS_RCU_READ, &mapping->flags);" in pg and
        "i_mmap_writable" not in pg,
      "reader_checks_eligibility_before_rcu_enter":
        "mapping_rcu_read(iocb->ki_filp->f_mapping)" in dispatch and
        "rcu_read_lock();" not in dispatch.split("mapping_rcu_read(iocb->ki_filp->f_mapping)",1)[0] and
        ordered(batch,"rcu_read_lock();","mapping_writably_mapped(mapping)",
                "pagefault_disable();","filemap_read_rcu_lookup("),
      "writer_counter_first_then_ext4_gp_then_vma":
        ordered(block,"mapping_map_writable(file->f_mapping);",
                "file->f_mapping->host->i_sb->s_magic == EXT4_SUPER_MAGIC",
                "synchronize_rcu();") and
        ordered(writer,"synchronize_rcu();","__mmap_region("),
      "writer_does_NOT_consult_AS_RCU_READ_for_gp":
        "mapping_rcu_read(" not in noncomment and
        "mapping_writably_mapped(" not in noncomment,
      "writer_gp_scoped_ext4_nonnull_inode":
        "if (file->f_mapping->host &&" in block and
        "file->f_mapping->host->i_sb->s_magic == EXT4_SUPER_MAGIC" in block,
      "writer_writable_count_balanced_after_attempt":
        ordered(writer,"mapping_map_writable(file->f_mapping)",
                "__mmap_region(", "mapping_unmap_writable(file->f_mapping);"),
      "writable_map_count_has_atomic_publish":
        "atomic_inc_unless_negative(&mapping->i_mmap_writable)" in fs,
      "no_reader_sleeps_during_rcu_copy":
        ordered(batch,"rcu_read_lock();","pagefault_disable();",
                "filemap_read_rcu_copy(","pagefault_enable();") and
        batch.rfind("rcu_read_unlock();")>batch.find("pagefault_enable();"),
      "no_G683_GP_in_filemap_fastpath":
        "synchronize_rcu();" not in batch,
      "RCU_GP_api_has_publication_barrier":
        "void synchronize_rcu(void)" in rcu and
        "Illegal synchronize_rcu() in RCU read-side critical section" in rcu,
    }


def mutants(original):
    wrong={
        "restore_racy_AS_RCU_READ_gate":("mm/vma.c",
            "file->f_mapping->host->i_sb->s_magic == EXT4_SUPER_MAGIC",
            "mapping_rcu_read(file->f_mapping)"),
        "skip_ext4_GP":("mm/vma.c",
            "file->f_mapping->host->i_sb->s_magic == EXT4_SUPER_MAGIC",
            "file->f_mapping->host->i_sb->s_magic != EXT4_SUPER_MAGIC"),
        "replace_GP_with_smp_mb":("mm/vma.c",
            "synchronize_rcu();","smp_mb();"),
        "remove_GP":("mm/vma.c",
            "synchronize_rcu();","; /* no grace */"),
        "remove_writable_counter_publish":("mm/vma.c",
            "int error = mapping_map_writable(file->f_mapping);",
            "int error = 0;"),
        "remove_ref_free_reader_RCU_lock":("mm/filemap.c",
            "rcu_read_lock();\n#ifdef CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT",
            "#ifdef CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT"),
        "disable_marker_eligibility_publish":("mm/filemap.c",
            "if (READ_ONCE(folio->mapping) == mapping)\n\t\tmapping_set_rcu_read(mapping);",
            "if (READ_ONCE(folio->mapping) == mapping)\n\t\t;"),
        "disable_eligibility_test":("include/linux/pagemap.h",
            "return test_bit(AS_RCU_READ, &mapping->flags);",
            "return true;"),
    }
    caught={}
    for name,(filename,needle,replace) in wrong.items():
        assert needle in original[filename], name
        changed=dict(original)
        changed[filename]=changed[filename].replace(needle,replace,1)
        bad=[id for id,pass_ in checks(changed).items() if not pass_]
        assert bad,(name,"mutation escaped")
        caught[name]=bad
    return caught


def herd():
    results={}
    for kind,(name,expected,pos,neg) in MODELS.items():
        path=ROOT/"tools/memory-model/litmus-tests"/(name+".litmus")
        content=path.read_text()
        assert content.startswith("C "+name+"\n")
        assert "P0(" in content and "P1(" in content and "P2(" in content
        assert "exists (1:r0=1" in content
        if kind=="unconditional_ext4_grace":
            assert content.count("synchronize_rcu();")==1
            assert "if (r3 == 0)" not in content
        else:
            assert "if (r3 == 0)" in content
        if kind=="reader_barrier_counterfactual":
            assert "if (r0 == 1) {\n   smp_mb();" in content
        process=subprocess.run(["herd7","-I","tools/memory-model",
                                "-conf","tools/memory-model/linux-kernel.cfg",
                                str(path.relative_to(ROOT))],
                               cwd=ROOT,capture_output=True,text=True,
                               timeout=55,check=True)
        m=re.search(r"(?m)^Observation (\S+) (Sometimes|Never|Always) (\d+) (\d+)\s*$",process.stdout)
        assert m,(name,process.stdout[-1100:])
        assert (m.group(1),m.group(2),int(m.group(3)),int(m.group(4))) == (
            name,expected,pos,neg),(name,m.groups())
        normalized=re.sub(r"(?m)^Time [^\n]*\n?","",process.stdout)
        results[kind]={
            "test":str(path.relative_to(ROOT)),"outcome":expected,
            "positive":pos,"negative":neg,
            "source_sha256":hashlib.sha256(content.encode()).hexdigest(),
            "normalized_herd7_sha256":hashlib.sha256(normalized.encode()).hexdigest()
        }
    return results


def main():
    guards=checks(SRC)
    assert len(guards)==14,len(guards)
    assert all(guards.values()),[k for k,v in guards.items() if not v]
    neg=mutants(SRC)
    assert len(neg)==8
    models=herd()
    assert models["old_eligibility_guard_race"]["positive"]>0
    assert models["unconditional_ext4_grace"]["positive"]==0
    assert models["writer_barrier_is_not_grace"]["positive"]>0
    assert models["reader_barrier_counterfactual"]["positive"]==0
    doc={
      "stage":"G683","source_guards":guards,"source_guards_passed":len(guards),
      "changed_source_mutants_rejected":neg,"actual_herd7_models":models,
      "change":"mm/vma.c: for CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT, ext4 shared-maywrite creation waits GP regardless of concurrently published AS_RCU_READ",
      "risk":"Unconditional GP on all ext4 shared-maywrite mmap in experiment, even with static key OFF; mmap_lock-held latency unmeasured",
      "limitations":[
       "LKMM scalar eligibility/count model with illustrative full writer barriers, not exact atomic bit RMW implementation.",
       "Neither an allowed execution nor its removal in the model proves real ARMv7 cache corruption or complete safety.",
       "Reader-barrier model is counterfactual only; no extra hot-read barrier is installed.",
       "Other writer paths, mmap_lock global deadlock graph, memory pressure and performance remain unproven.",
       "S2/S3 BLOCKED; kernel runtime change remains STAGED NOT INSTALLED, physical G650 stays RCU OFF."
      ]
    }
    OUT.write_text(json.dumps(doc,ensure_ascii=False,indent=2)+"\n")
    print(f"G683 {len(guards)}/14 real source gates PASS, {len(neg)}/8 source mutants REJECT")
    print("G683 real LKMM:",[(k,v["outcome"],v["positive"],v["negative"]) for k,v in models.items()])


if __name__=="__main__":
    main()
