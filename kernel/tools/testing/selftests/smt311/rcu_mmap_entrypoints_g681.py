#!/usr/bin/env python3
"""SM-T311 G681: source-specific positive-writable-map origin and lock wait audit.

This audit is intentionally narrow: it checks ordering in actual C source and
detects meaningful modifications of source snippets. It does not execute
kernel concurrency, prove all lockdep dependency graphs, or promote RCU ON.
"""
from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parents[4]
OUT = ROOT / "Documentation/mm/sm-t311-rcu-mmap-entrypoints-g681.json"
PATHS = ("mm/filemap.c", "mm/vma.c", "mm/mmap.c", "mm/mprotect.c",
         "mm/mremap.c", "mm/memfd.c", "include/linux/fs.h",
         "include/linux/mm.h", "mm/vma_internal.h")
SRC = {x: (ROOT / x).read_text() for x in PATHS}


def between(s, start, end):
    assert start in s, start
    r = s.split(start, 1)[1]
    assert end in r, end
    return r.split(end, 1)[0]


def order(s,*needles):
    p=[s.find(t) for t in needles]
    return all(x >= 0 for x in p) and p == sorted(p) and len(set(p)) == len(p)


def checks(src):
    fm, vma, mmap, mp, remap, memfd, fs, mmh, intr = (src[p] for p in PATHS)
    r=between(fm,"static bool filemap_read_rcu_batch(", "size_t filemap_read_rcu(")
    m=between(vma, "unsigned long mmap_region(", "/**\n * do_brk_flags(")
    fork=between(mmap,"__latent_entropy int dup_mmap(", "fail_nomem_anon_vma_fork:")
    # Other markers intentionally chosen inside bounded functions.
    link=between(vma, "static void __vma_link_file(", "static void __remove_shared_vm_struct(")
    prep=between(vma, "static void vma_prepare(", "static void vma_complete(")
    proc=between(vma, "static void __remove_shared_vm_struct(", "/*\n * vma has")
    shrink=between(memfd, "if ((seals & F_SEAL_WRITE)", "*file_seals |= seals;")
    mark=between(fm, "static noinline void filemap_mark_hot(", "void filemap_read_accessed(")
    return {
      "rcu_read_lock_before_writable_count":
        order(r, "rcu_read_lock();","mapping_writably_mapped(mapping)",
              "pagefault_disable();", "filemap_read_rcu_copy("),
      "ref_free_rcu_copy_no_mmap_lock":
        "mmap_read_lock(" not in r and "mmap_write_lock(" not in r and
        "mmap_lock" not in between(fm,"static size_t filemap_read_rcu_copy(",
                                  "/* Bytes one RCU read section"),
      "user_faults_disabled_before_copy":
        order(r,"pagefault_disable();","filemap_read_rcu_copy(","pagefault_enable();"),
      "rcu_reader_releases_after_user_faults_restored":
        r.rfind("rcu_read_unlock();")>r.find("pagefault_enable();"),
      "mmap_writer_requires_mmap_lock":
        "mmap_assert_write_locked(current->mm);" in m,
      "mmap_writer_publish_count_then_gp_then_vma":
        order(m,"mapping_map_writable(file->f_mapping);",
              "synchronize_rcu();","writable_file_mapping = true;",
              "__mmap_region("),
      "mmap_gp_predicated_on_ext4_magic":
        "file->f_mapping->host->i_sb->s_magic == EXT4_SUPER_MAGIC" in m and
        "mapping_rcu_read(file->f_mapping)" not in m,
      "mmap_temp_count_balanced":
        order(m,"mapping_map_writable(file->f_mapping)",
              "__mmap_region(", "mapping_unmap_writable(file->f_mapping);"),
      "other_positive_counter_origin_is_prior_existing_vma":
        "if (vma_is_shared_maywrite(vma))" in link and
        "mapping_allow_writable(mapping);" in link and
        "vma_prepare(" in vma and "vp->insert" in prep and
        "vp->insert->vm_file->f_mapping" in prep,
      "vma_split_keeps_original_vma":
        "vp->vma" in prep and "vp->insert" in prep,
      "fork_clones_prior_vma_and_adds_positive_count":
        order(fork,"vm_area_dup(mpnt);", "mapping_allow_writable(mapping);",
              "mapping_rmap_tree_insert_after(tmp, mpnt, mapping);") and
        "if (vma_is_shared_maywrite(tmp))" in fork,
      "fork_holds_parent_mmap_lock":
        order(fork,"mmap_write_lock_killable(oldmm)", "for_each_vma(vmi, mpnt)",
              "vm_area_dup(mpnt);"),
      "mprotect_does_not_increment_mapping_count":
        "mapping_map_writable(" not in mp and
        "mapping_allow_writable(" not in mp and
        "vma_modify_flags(" in mp and "VMA_WRITE_BIT" in mp and
        "VMA_MAYWRITE_BIT" in mmh,
      "mremap_does_not_increment_mapping_count":
        "mapping_map_writable(" not in remap and
        "mapping_allow_writable(" not in remap and
        "vma" in remap,
      "memfd_rollback_only_after_deny":
        order(shrink, "mapping_deny_writable(file->f_mapping)",
              "memfd_wait_for_pins(file->f_mapping)",
              "mapping_allow_writable(file->f_mapping);"),
      "counter_counts_shared_maywrite_not_only_current_write":
        "atomic_read(&mapping->i_mmap_writable) > 0" in fs and
        "atomic_inc_unless_negative(&mapping->i_mmap_writable)" in fs and
        "atomic_inc(&mapping->i_mmap_writable);" in fs and
        "vma_is_shared_maywrite(" in mmh,
      "unmap_removes_positive_count":
        "mapping_unmap_writable(mapping);" in proc,
      "rcu_mark_precedes_as_rcu_read_enable":
        order(mark, "folio_test_set_rcu_free(folio)",
              "mapping_set_rcu_read(mapping);"),
      "reader_is_eligible_only_with_as_rcu_read":
        "mapping_rcu_read(iocb->ki_filp->f_mapping)" in fm,
      "RCU_experiment_compile_time_guard_only":
        "#ifdef CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT" in m,
    }


def mutants(source):
    cases={
      "reader_check_before_rcu":("mm/filemap.c",
        "rcu_read_lock();\n#ifdef CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT",
        "#ifdef CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT"),
      "reader_may_sleep_on_fault":("mm/filemap.c",
        "pagefault_disable();\n\twhile (iov_iter_count(iter))",
        "while (iov_iter_count(iter))"),
      "new_mapping_gp_deleted":("mm/vma.c",
        "if (file->f_mapping->host &&\n\t\t    file->f_mapping->host->i_sb->s_magic == EXT4_SUPER_MAGIC)\n\t\t\tsynchronize_rcu();",
        "if (file->f_mapping->host &&\n\t\t    file->f_mapping->host->i_sb->s_magic == EXT4_SUPER_MAGIC)\n\t\t\t;"),
      "new_mapping_count_after_gp":("mm/vma.c",
        "int error = mapping_map_writable(file->f_mapping);",
        "int error = 0;"),
      "fork_does_not_copy_existing_vma":("mm/mmap.c",
        "tmp = vm_area_dup(mpnt);","tmp = NULL;"),
      "fork_count_untracked":("mm/mmap.c",
        "mapping_allow_writable(mapping);",""),
      "split_count_untracked":("mm/vma.c",
        "mapping_allow_writable(mapping);",""),
      "remove_mutual_exclusion_vma":("mm/vma.c",
        "mmap_assert_write_locked(current->mm);",""),
      "remove_memory_refcheck":("mm/filemap.c",
        "mapping_rcu_read(iocb->ki_filp->f_mapping)",
        "true /* bypass mapping eligibility */"),
      "memfd_writable_rollback_missing":("mm/memfd.c",
        "mapping_allow_writable(file->f_mapping);",""),
    }
    rejected={}
    for name,(filename,before,after) in cases.items():
        if before not in source[filename]:
            raise AssertionError("mutation precondition absent: "+name)
        modified=dict(source)
        modified[filename]=modified[filename].replace(before,after,1)
        failed=[key for key,v in checks(modified).items() if not v]
        if not failed:
            raise AssertionError("mutation escaped: "+name)
        rejected[name]=failed
    return rejected


def main():
    passed=checks(SRC)
    failed=[key for key,v in passed.items() if not v]
    if failed:
        raise AssertionError("G681 live source mismatch: "+repr(failed))
    rejections=mutants(SRC)
    result={
        "stage":"G681", "scope":"source-anchored writable VMA origin audit",
        "source_guards":passed,"source_guards_passed":len(passed),
        "mutated_source_rejected":rejections,
        "conclusions":[
          "mmap_region is source-visible first transition from zero to positive shared-maywrite count.",
          "fork duplicates an existing positive-count source VMA while parent remains mapped.",
          "vma_prepare split inserts beside a pre-existing source VMA; it increments the existing source count.",
          "mprotect changes write permission of previously VM_MAYWRITE shared mapping; no count increment in mm/mprotect.c.",
          "mremap moves/extends existing VMAs, no new writable count increment found.",
          "memfd mapping_allow_writable here only rolls back deny_writable on an error and does not create an ext4 VMA.",
          "filemap_read_rcu_batch does not take mmap_lock and disables faults during its bounded copy."
        ],
        "unproven":[
          "Full lockdep proof for all kernel RCU read-side critical sections while mmap_lock write is held.",
          "Real completion/latency of synchronize_rcu under mmap_lock on Cortex-A9, especially under page faults.",
          "All driver/filesystem-specific mm hooks and archived code outside inspected entrypoints.",
          "Cache coherency under actual ARMv7 aliasing; no physical RCU ON testing.",
        ],
        "safety":"S0 PASS; S1 ACTIVE; S2/S3 BLOCKED; runtime still staged not installed",
    }
    OUT.write_text(json.dumps(result,indent=2,ensure_ascii=False)+"\n")
    print(f"G681 {len(passed)} source predicates pass and {len(rejections)} modified-source mutants rejected")


if __name__=="__main__":
    main()
