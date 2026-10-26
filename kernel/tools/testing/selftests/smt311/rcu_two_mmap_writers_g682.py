#!/usr/bin/env python3
"""G682: real LKMM check of two competing new MAP_SHARED writers.

Reject the tempting optimization of synchronize_rcu only for a mapping's
zero->positive writable-count transition: another process may hold an
unpublished positive reservation concurrently. This is an abstract LKMM
counterexample, not proof of a physical corruption or global lock safety.
"""
from pathlib import Path
import hashlib
import json
import re
import subprocess

K = Path(__file__).resolve().parents[4]
OUT = K / "Documentation/mm/sm-t311-rcu-two-mmap-writers-g682.json"
PATHS = ("mm/vma.c","mm/filemap.c","mm/mmap.c",
         "include/linux/fs.h","include/linux/mm.h")
SRC = {n:(K/n).read_text() for n in PATHS}
MODELS = {
  "one_writer_skips_gp":("SMT311+MmapTwoWritersSkipGraceG682","Sometimes",1,5),
  "both_writers_wait_for_gp":("SMT311+MmapTwoWritersEachGraceG682","Never",0,5),
  "only_memory_barrier":("SMT311+MmapTwoWritersOnlyBarrierG682","Sometimes",1,5),
}

def region(text,a,b):
    assert a in text, a
    following=text.split(a,1)[1]
    assert b in following,b
    return following.split(b,1)[0]

def ordered(s,*xs):
    vals=[s.find(x) for x in xs]
    return all(v >= 0 for v in vals) and vals == sorted(vals) and len(set(vals)) == len(vals)

def source_checks(src):
    vma, reader, mmap, fs, mm = (src[x] for x in PATHS)
    writer=region(vma,"unsigned long mmap_region(", "/**\n * do_brk_flags(")
    fast=region(reader, "static bool filemap_read_rcu_batch(", "size_t filemap_read_rcu(")
    mark=region(reader,"static noinline void filemap_mark_hot(", "void filemap_read_accessed(")
    fork=region(mmap,"__latent_entropy int dup_mmap(", "fail_nomem_anon_vma_fork:")
    block=region(writer, "if (file && is_shared_maywrite(&vma_flags)) {",
                 "writable_file_mapping = true;")
    block_code=re.sub(r"/\*.*?\*/", "", block, flags=re.S)
    return {
        "writable_count_global_file_mapping":
            "file->f_mapping" in block and
            "i_mmap_writable" in fs,
        "new_writer_lock_per_mm_not_file":
            "mmap_assert_write_locked(current->mm);" in writer,
        "writable_count_atomic_increment_unless_negative":
            "atomic_inc_unless_negative(&mapping->i_mmap_writable)" in fs,
        "writer_positive_count_before_grace":
            ordered(block, "mapping_map_writable(file->f_mapping);",
                    "file->f_mapping->host->i_sb->s_magic == EXT4_SUPER_MAGIC",
                    "synchronize_rcu();"),
        "writer_gp_independent_of_counter_prior_value":
            "file->f_mapping->host->i_sb->s_magic == EXT4_SUPER_MAGIC)\n\t\t\tsynchronize_rcu();" in block
            and "mapping_writably_mapped(" not in block_code
            and "atomic_read(" not in block_code,
        "writer_gp_before_vma_publish":
            ordered(writer, "synchronize_rcu();",
                    "writable_file_mapping = true;", "__mmap_region("),
        "writer_counter_unwound_after_vma_attempt":
            ordered(writer, "mapping_map_writable(file->f_mapping);",
                    "__mmap_region(","mapping_unmap_writable(file->f_mapping);"),
        "reader_rcu_lock_before_count_check":
            ordered(fast,"rcu_read_lock();",
                    "mapping_writably_mapped(mapping)", "pagefault_disable();"),
        "reader_copies_while_rcu_protected":
            ordered(fast,"rcu_read_lock();", "pagefault_disable();",
                    "filemap_read_rcu_copy(", "pagefault_enable();")
            and fast.rfind("rcu_read_unlock();")>fast.find("pagefault_enable();"),
        "reader_no_mmap_lock":
            "mmap_read_lock(" not in fast and "mmap_write_lock(" not in fast,
        "eligibility_bit_persistent_after_mark":
            ordered(mark, "folio_test_set_rcu_free(folio)",
                    "mapping_set_rcu_read(mapping);")
            and "if (READ_ONCE(folio->mapping) == mapping)\n\t\tmapping_set_rcu_read(mapping);" in mark
            and "mapping_rcu_read(iocb->ki_filp->f_mapping)" in reader,
        "fork_duplicates_counted_parent":
            ordered(fork, "vm_area_dup(mpnt);",
                    "mapping_allow_writable(mapping);",
                    "mapping_rmap_tree_insert_after(tmp, mpnt, mapping);"),
        "shared_maywrite_includes_maywrite_not_only_write":
            "vma_flags_test_all(flags, VMA_SHARED_BIT, VMA_MAYWRITE_BIT)" in mm,
    }

def source_mutants(src):
    modifications={
     "skip_gp_for_existing_count":("mm/vma.c",
        "if (file->f_mapping->host &&\n\t\t    file->f_mapping->host->i_sb->s_magic == EXT4_SUPER_MAGIC)\n\t\t\tsynchronize_rcu();",
        "if (file->f_mapping->host &&\n\t\t    file->f_mapping->host->i_sb->s_magic == EXT4_SUPER_MAGIC && !mapping_writably_mapped(file->f_mapping))\n\t\tsynchronize_rcu();"),
     "omit_gp":("mm/vma.c","synchronize_rcu();","; /* omitted GP */"),
     "substitute_memory_barrier":("mm/vma.c","synchronize_rcu();","smp_mb();"),
     "move_reader_count_check_before_rcu":("mm/filemap.c",
        "rcu_read_lock();\n#ifdef CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT",
        "#ifdef CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT"),
     "remove_writer_writable_counter":("mm/vma.c",
        "int error = mapping_map_writable(file->f_mapping);",
        "int error = 0;"),
     "omit_filemap_rcu_eligibility_bit":("mm/filemap.c",
        "if (READ_ONCE(folio->mapping) == mapping)\n\t\tmapping_set_rcu_read(mapping);",
        "if (READ_ONCE(folio->mapping) == mapping)\n\t\t; /* no eligibility bit */"),
    }
    caught={}
    for name,(file,old,new) in modifications.items():
        assert old in src[file],name
        copy=dict(src)
        copy[file]=copy[file].replace(old,new,1)
        failed=[k for k,v in source_checks(copy).items() if not v]
        assert failed,(name, "source mutant escaped")
        caught[name]=failed
    return caught

def real_lkmm():
    out={}
    for name,(model,expected,positive,negative) in MODELS.items():
        path=K/"tools/memory-model/litmus-tests"/(model+".litmus")
        content=path.read_text()
        assert content.startswith("C "+model+"\n"),model
        assert "P0(" in content and "P1(" in content and "P2(" in content
        assert content.count("synchronize_rcu();") == (
            2 if name=="both_writers_wait_for_gp" else 1)
        assert "exists (0:r0=0" in content
        proc=subprocess.run(["herd7","-I","tools/memory-model","-conf",
                             "tools/memory-model/linux-kernel.cfg",
                             str(path.relative_to(K))],cwd=K,
                            text=True,capture_output=True,timeout=55,check=True)
        match=re.search(r"(?m)^Observation (\S+) (Never|Sometimes|Always) (\d+) (\d+)\s*$",
                        proc.stdout)
        assert match,proc.stdout[-1000:]
        assert (match.group(1),match.group(2),int(match.group(3)),int(match.group(4)))==(
            model,expected,positive,negative),(model,match.groups())
        normalized=re.sub(r"(?m)^Time [^\n]*\n?","",proc.stdout)
        out[name]={
            "litmus":str(path.relative_to(K)),
            "observation":match.group(0),"outcome":expected,
            "positive":positive,"negative":negative,
            "source_sha256":hashlib.sha256(content.encode()).hexdigest(),
            "normalized_herd7_sha256":hashlib.sha256(normalized.encode()).hexdigest(),
        }
    return out

def main():
    guards=source_checks(SRC)
    assert len(guards)==13,len(guards)
    assert all(guards.values()),[x for x,v in guards.items() if not v]
    mutants=source_mutants(SRC)
    models=real_lkmm()
    assert len(mutants)==6
    assert models["both_writers_wait_for_gp"]["positive"]==0
    assert all(models[x]["positive"]>0
               for x in ("one_writer_skips_gp","only_memory_barrier"))
    report={
        "stage":"G682", "kind":"actual LKMM herd7 plus source-anchored gate",
        "source_guards":guards,"source_guards_pass":len(guards),
        "source_mutants_rejected":mutants,"herd7_models":models,
        "conclusion":"A positive i_mmap_writable value can represent a reservation from a different concurrent mmap that has not yet waited its GP or published its VMA. Do not skip GP merely because previous count is nonzero.",
        "constraints":[
          "Current G680 GP per new shared-writable mmap is retained.",
          "The writers are separate mm_struct mmap locks; count is per file mapping.",
          "This abstract two-writer LKMM diagnostic is not full VMA/allocator modeling or hardware ARMv7 validation.",
          "Global mmap_lock/RCU lockdep graph and measured GP latency remain unproven.",
          "No executable source change in G682; G674/G676/G680 remain STAGED NOT INSTALLED and no-ref RCU is OFF.",
        ]
    }
    OUT.write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n")
    print(f"G682 source {len(guards)}/{len(guards)} PASS, mutants {len(mutants)}/{len(mutants)} REJECT")
    print("G682 real LKMM:",[(n,p["outcome"],p["positive"],p["negative"]) for n,p in models.items()])

if __name__=="__main__":
    main()
