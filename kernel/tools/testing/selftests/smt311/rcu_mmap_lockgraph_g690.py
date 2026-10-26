#!/usr/bin/env python3
"""G690 SM-T311: source-anchored mmap_lock / RCU grace wait contract audit.

Checks exact caller/callee RCU state transitions in mm/mmap_lock.c and
fs/proc/task_mmu.c when CONFIG_PER_VMA_LOCK=y. A tiny finite interleaving
model compares correct RCU-drop-before-blocking reader with a
counterfactual reader which holds RCU while waiting on mmap_read_lock.
This is NOT full kernel lockdep and does NOT prove no global cycle.
"""
from pathlib import Path
from collections import deque
import json
import re
K=Path(__file__).resolve().parents[4]
OUT=K/"Documentation/mm/sm-t311-rcu-mmap-lockgraph-g690.json"
PATHS=("mm/mmap_lock.c","mm/vma.c","fs/proc/task_mmu.c",
       "include/linux/mmap_lock.h","mm/filemap.c")
SRC={p:(K/p).read_text() for p in PATHS}
CFG=K.parent/"build/g686-reproducible-exp-on/.config"

def part(s,start,end):
    assert start in s,start
    t=s.split(start,1)[1]
    assert end in t,end
    return t.split(end,1)[0]

def ordered(s,*args):
    xs=[s.find(x) for x in args]
    return all(n>=0 for n in xs) and xs==sorted(xs) and len(xs)==len(set(xs))

def gates(s):
    mm,vma,proc,hdr,fil=[s[x] for x in PATHS]
    r=part(mm,"static inline struct vm_area_struct *vma_start_read(",
           "struct vm_area_struct *lock_vma_under_rcu(")
    get=part(mm,"struct vm_area_struct *lock_next_vma(", "#endif /* CONFIG_PER_VMA_LOCK */")
    fallback=part(mm,"static struct vm_area_struct *lock_next_vma_under_mmap_lock(",
                 "struct vm_area_struct *lock_next_vma(")
    procget=part(proc,"static struct vm_area_struct *get_next_vma(",
                 "static inline bool fallback_to_mmap_lock(")
    procfall=part(proc,"static inline bool fallback_to_mmap_lock(",
                  "static inline void drop_rcu(")
    procrange=part(proc,"static inline bool lock_vma_range(",
                   "static inline void unlock_vma_range(")
    query=part(proc,"static struct vm_area_struct *query_vma_find_by_addr(",
               "#else /* CONFIG_PER_VMA_LOCK */")
    writer=part(vma,"unsigned long mmap_region(", "/**\n * do_brk_flags(")
    batch=part(fil,"static bool filemap_read_rcu_batch(", "size_t filemap_read_rcu(")
    before_err=part(r,"err:\n", "err_unstable:")
    err_unstable=part(r,"err_unstable:\n","return NULL;")
    return {
      "PER_VMA_LOCK_supported_on_target":
          "CONFIG_PER_VMA_LOCK=y" in CFG.read_text(),
      "mmap_region_has_writer_lock_assertion":
          "mmap_assert_write_locked(current->mm);" in writer,
      "ext4_writer_counter_then_GP_then_VMA":
          ordered(writer,"mapping_map_writable(file->f_mapping);",
                  "synchronize_rcu();","__mmap_region(") and
          "EXT4_SUPER_MAGIC" in writer,
      "RCU_GP_does_not_hold_reader_lock_in_mmap_region":
          "rcu_read_lock();" not in writer,
      "read_helper_requires_RCU_held":
          "RCU_LOCKDEP_WARN(!rcu_read_lock_held()" in r,
      "read_helper_on_error_releases_RCU":
          "rcu_read_unlock();" in before_err,
      "read_helper_on_unstable_error_releases_RCU":
          "rcu_read_unlock();" in err_unstable,
      "read_helper_error_contract":
          "but upon error\n *            IT IS RELEASED" in mm,
      "caller_failed_lock_falls_back_after_helper":
          ordered(get,"vma_start_read(mm, vma);","if (IS_ERR_OR_NULL(vma))",
                  "goto fallback;") and "rcu_read_unlock();" in before_err,
      "caller_locked_path_explicit_drop":
          ordered(get,"fallback_unlock:\n\trcu_read_unlock();",
                  "vma_end_read(vma);","fallback:\n\tvma = lock_next_vma_under_mmap_lock("),
      "fallback_uses_sleepable_mmap_lock":
          "mmap_read_lock_killable(mm);" in fallback,
      "caller_reenters_RCU_after_sleepable_fallback":
          "fallback:\n\tvma = lock_next_vma_under_mmap_lock(mm, vmi, from_addr);\n\trcu_read_lock();" in get,
      "proc_scan_initially_takes_RCU":
          "rcu_read_lock();" in procrange,
      "proc_scan_delegates_to_guarded_helper":
          "lock_next_vma(lock_ctx->mm, &priv->iter, last_pos)" in procget,
      "proc_explicit_mmap_fallback_unlocks_first":
          ordered(procfall,"rcu_read_unlock();","mmap_read_lock(lock_ctx->mm);"),
      "proc_query_ends_RCU_before_fallback_lock":
          ordered(query,"vma = lock_next_vma(mm, &vmi, addr);",
                  "rcu_read_unlock();","mmap_read_lock(mm);"),
      "header_requires_RCU_on_lock_next_vma":
          "Should be called from read RCU section." in hdr,
      "experimental_fast_reader_has_no_mmap_wait":
          "mmap_read_lock(" not in batch and
          "mmap_write_lock(" not in batch and
          ordered(batch,"rcu_read_lock();","pagefault_disable();",
                  "filemap_read_rcu_copy(","pagefault_enable();"),
      "experimental_fast_reader_exits_RCU":
          batch.rfind("rcu_read_unlock();")>batch.find("pagefault_enable();")
    }

# Each state (wpc,rpc,wlocked,rlocked,rcu_reading).
# The model conservatively permits a writer's GP to complete only when
# no RCU readers remain, stronger than actual pre-existing-reader semantics.
# It is a conceptual wait-for graph test, not a Linux scheduler/lockdep test.
W_OPS=("WLOCK","GP","WUNLOCK")
R_GOOD=("RLOCK","RUNLOCK","MLOCK","MUNLOCK","RLOCK","RUNLOCK")
R_BAD=("RLOCK","MLOCK","MUNLOCK","RUNLOCK")

def explore(reader):
    init=(0,0,False,False,False)
    queue=deque([(init,[])])
    seen={init}
    deadlocked=None
    finished=False
    schedules=0
    while queue:
        state,hist=queue.popleft()
        wpc,rpc,writer_lock,reader_lock,reading=state
        if wpc==len(W_OPS) and rpc==len(reader):
            finished=True
            schedules+=1
            continue
        allowed=[]
        if wpc<len(W_OPS):
            op=W_OPS[wpc]
            ok=(op=="WLOCK" and not reader_lock and not writer_lock or
                op=="GP" and writer_lock and not reading or
                op=="WUNLOCK" and writer_lock)
            if ok:
                allowed.append(((wpc+1,rpc,
                     op!="WUNLOCK" if op in ("WLOCK","WUNLOCK") else writer_lock,
                     reader_lock,reading),f"W:{op}"))
        if rpc<len(reader):
            op=reader[rpc]
            ok=(op=="RLOCK" and not reading or
                op=="RUNLOCK" and reading or
                op=="MLOCK" and not writer_lock and not reader_lock or
                op=="MUNLOCK" and reader_lock)
            if ok:
                allowed.append(((wpc,rpc+1,writer_lock,
                    op!="MUNLOCK" if op in ("MLOCK","MUNLOCK") else reader_lock,
                    op!="RUNLOCK" if op in ("RLOCK","RUNLOCK") else reading),f"R:{op}"))
        if not allowed and deadlocked is None:
            deadlocked={"state":state,"wants_writer":W_OPS[wpc] if wpc<len(W_OPS) else None,
                        "wants_reader":reader[rpc] if rpc<len(reader) else None,
                        "trace":hist}
        for next_state,event in allowed:
            if next_state not in seen:
                seen.add(next_state)
                queue.append((next_state,hist+[event]))
    return {"reachable_states":len(seen),"finished":finished,
            "deadlock_reachable":deadlocked is not None,"deadlock_witness":deadlocked}

def source_mutants():
    wrong={
       "remove_helper_error_RCU_drop":("mm/mmap_lock.c",
            "err:\n\trcu_read_unlock();","err:\n\t/* dropped unlock */"),
       "remove_helper_unstable_RCU_drop":("mm/mmap_lock.c",
            "rcu_read_unlock();\n\tmmgrab(other_mm);",
            "/* dropped unlock */\n\tmmgrab(other_mm);"),
       "direct_fallback_skips_rcu_drop":("mm/mmap_lock.c",
            "fallback_unlock:\n\trcu_read_unlock();",
            "fallback_unlock:\n\t/* no unlock */"),
       "reader_reenters_RCU_before_blocking":("mm/mmap_lock.c",
            "fallback:\n\tvma = lock_next_vma_under_mmap_lock(mm, vmi, from_addr);\n\trcu_read_lock();",
            "fallback:\n\trcu_read_lock();\n\tvma = lock_next_vma_under_mmap_lock(mm, vmi, from_addr);"),
       "proc_mmap_fallback_reversed":("fs/proc/task_mmu.c",
            "rcu_read_unlock();\n\tmmap_read_lock(lock_ctx->mm);",
            "mmap_read_lock(lock_ctx->mm);\n\trcu_read_unlock();"),
       "remove_proc_query_RCU_release":("fs/proc/task_mmu.c",
            "vma = lock_next_vma(mm, &vmi, addr);\n\trcu_read_unlock();",
            "vma = lock_next_vma(mm, &vmi, addr);"),
       "remove_writer_GP":("mm/vma.c","synchronize_rcu();","smp_mb();"),
       "remove_writer_mmap_assertion":("mm/vma.c",
            "mmap_assert_write_locked(current->mm);","/* no mmap assert */"),
       "sleep_within_fast_rcu_reader":("mm/filemap.c",
            "pagefault_disable();\n\twhile (iov_iter_count(iter))",
            "mmap_read_lock(iocb->ki_filp->f_mapping->host->i_mapping->host->i_private);\n\tpagefault_disable();\n\twhile (iov_iter_count(iter))"),
    }
    caught={}
    for name,(file,needle,replacement) in wrong.items():
        assert needle in SRC[file],name
        new=dict(SRC)
        new[file]=new[file].replace(needle,replacement,1)
        fails=[k for k,v in gates(new).items() if not v]
        assert fails,(name,"escaped test gates")
        caught[name]=fails
    return caught

def main():
    observed=gates(SRC)
    assert len(observed)==19,len(observed)
    assert all(observed.values()),[k for k,v in observed.items() if not v]
    caught=source_mutants()
    assert len(caught)==9
    safe=explore(R_GOOD)
    unsafe=explore(R_BAD)
    assert safe["finished"] and not safe["deadlock_reachable"],safe
    assert unsafe["finished"] and unsafe["deadlock_reachable"],unsafe
    assert unsafe["deadlock_witness"]["wants_reader"]=="MLOCK",unsafe
    assert unsafe["deadlock_witness"]["wants_writer"]=="GP",unsafe
    report={
       "stage":"G690",
       "topic":"source-anchored RCU/mmap_lock wait-for graph: PER_VMA_LOCK fallback",
       "source_guards":observed,"passed_source_guards":len(observed),
       "modified_real_source_mutations_rejected":caught,
       "bounded_interleaving":{
           "actual_contract_reader_releases_RCU_before_mmap_read_lock":safe,
           "counterfactual_reader_blocks_on_mmap_lock_holding_RCU":unsafe
       },
       "result":"One candidate direct inversion in /proc/maps VMA-lock fallback is avoided because vma_start_read() releases RCU on error and explicit fallback releases it first. This is NOT a global lockdep proof.",
       "limitations":[
           "A modeled cycle in a counterfactual source is not a reproduced deadlock in the actual kernel.",
           "The finite-state model conservatively waits for all readers at GP and abstracts preemption, refcounts and cache ordering.",
           "Other RCU read-side sections, callbacks, unrelated per-mm locks and global edges not exhaustively audited.",
           "No PREEMPT_RCU physical lockdep execution or mmap p99 measurement; CONFIG_PROVE_LOCKING=n in current host build.",
           "PFN/ABA and ARMv7 VIPT cache aliases remain unproven; S2/S3 BLOCKED.",
           "G683 conservative EXT4 GP under mmap write lock still has cost even when experiment runtime key OFF.",
           "No new kernel executable source paths and no flash."
       ],
       "physical_kernel":"G650 unchanged",
       "staged_kernel_executable_files_NOT_INSTALLED":[
           "mm/filemap.c","mm/page_alloc.c","mm/shmem.c","mm/vma.c"]
    }
    OUT.write_text(json.dumps(report,indent=2,ensure_ascii=False)+"\n")
    print("G690 source checks",len(observed),"/19 PASS,",
          len(caught),"/9 deliberately altered C snippets rejected")
    print("G690 good RCU drop before lock:",safe)
    print("G690 bad lock under RCU:",unsafe)

if __name__=="__main__":
    main()
