#!/usr/bin/env python3
"""G685: source-anchored double-check of no-ref RCU static key on disable.

The first key gate precedes RCU, hence a reader can be preempted between
the gate and rcu_read_lock. Guard with a recheck *inside* the read-side
critical section, before reading unpinned folios.

This is a minimal diagnostic of a concurrent disable, NOT a general
proof of the no-ref protocol, and especially not license to omit
per-writer mmap GPs while OFF.
"""
from pathlib import Path
import hashlib, json, re, subprocess

K=Path(__file__).resolve().parents[4]
OUT=K/"Documentation/mm/sm-t311-rcu-disable-reader-g685.json"
CFG=K.parent/"build/g633-armv7-rcu-zone-guard/.config"
OBJ=K.parent/"build/g633-armv7-rcu-zone-guard/mm/filemap.o"
SRC_PATHS=("mm/filemap.c","mm/vma.c","include/linux/pagemap.h",
           "include/linux/jump_label.h")
SRC={k:(K/k).read_text() for k in SRC_PATHS}
MODELS={
    "old_key_check_outside_rcu":("SMT311+DisableStaleReaderNoRecheckG685","Sometimes",1,4),
    "second_check_inside_rcu":("SMT311+DisableStaleReaderRecheckG685","Never",0,6),
}

def region(s,a,b):
    assert a in s,a
    t=s.split(a,1)[1]
    assert b in t,b
    return t.split(b,1)[0]

def ordered(t,*terms):
    where=[t.find(z) for z in terms]
    return all(z>=0 for z in where) and where==sorted(where) and len(set(where))==len(where)

def gates(s):
    filemap,vma,pg,jump=(s[x] for x in SRC_PATHS)
    first=region(filemap,"size_t filemap_read_rcu(", "ssize_t filemap_read(")
    batch=region(filemap,"static bool filemap_read_rcu_batch(", "size_t filemap_read_rcu(")
    toggle=region(filemap,"static int lockless_read_set(", "DEFINE_DEBUGFS_ATTRIBUTE(")
    writer=region(vma,"unsigned long mmap_region(","/**\n * do_brk_flags(")
    return {
      "static_key_default_off":"static DEFINE_STATIC_KEY_FALSE(lockless_read_key);" in filemap,
      "runtime_key_first_check_before_reader_batch":
          ordered(first,"lockless_read_enabled()","filemap_read_rcu_batch("),
      "second_check_after_entering_rcu_before_mmap":
          ordered(batch,"rcu_read_lock();",
                  "if (unlikely(!lockless_read_enabled()))",
                  "mapping_writably_mapped(mapping)",
                  "pagefault_disable();", "filemap_read_rcu_lookup("),
      "second_check_compiled_only_for_experiment":
          '#ifdef CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT\n\t/*\n\t * filemap_read_rcu()' in batch,
      "second_check_false_branch_unlocks_and_falls_back":
          "if (unlikely(!lockless_read_enabled())) {\n\t\trcu_read_unlock();\n\t\treturn false;\n\t}" in batch,
      "mmap_guard_unlocks_and_falls_back":
          "mapping_writably_mapped(mapping)) {\n\t\trcu_read_unlock();\n\t\treturn false;" in batch,
      "no_speculative_copy_if_no_key":
          ordered(batch,"if (unlikely(!lockless_read_enabled()))",
                  "filemap_read_rcu_lookup(", "filemap_read_rcu_copy("),
      "user_pagefault_disabled_for_copy":
          ordered(batch,"pagefault_disable();","filemap_read_rcu_copy(",
                  "pagefault_enable();") and
          batch.rfind("rcu_read_unlock();")>batch.find("pagefault_enable();"),
      "toggle_disable_available":
          "static_branch_disable(&lockless_read_key);" in toggle,
      "toggle_enable_available":
          "static_branch_enable(&lockless_read_key);" in toggle,
      "reader_key_does_not_serialize_mmap_writers":
          "mmap_lock(" not in toggle and "mapping_map_writable(" not in toggle,
      "mmap_writer_ext4_gp_retained":
          "file->f_mapping->host->i_sb->s_magic == EXT4_SUPER_MAGIC" in writer and
          "synchronize_rcu();" in writer,
      "mapping_eligibility_still_checked":
          "mapping_rcu_read(iocb->ki_filp->f_mapping)" in first and
          "return test_bit(AS_RCU_READ, &mapping->flags)" in pg,
      "CONFIG_JUMP_LABEL_fallback":
          "static_key_enabled(&(x)->key)" in jump,
      "no_new_GP_in_reader_batch":
          "synchronize_rcu();" not in batch,
      "actual_exp_config_on":
          "CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y" in CFG.read_text()
          and "CONFIG_PAGE_RCU_FREE=y" in CFG.read_text()
          and "CONFIG_DEBUG_FS=y" in CFG.read_text(),
    }

def real_source_mutations():
    mods={
        "remove_internal_key_recheck":(
            "mm/filemap.c","if (unlikely(!lockless_read_enabled())) {",
            "if (false) {"),
        "skip_internal_unlock":(
            "mm/filemap.c",
            "if (unlikely(!lockless_read_enabled())) {\n\t\trcu_read_unlock();",
            "if (unlikely(!lockless_read_enabled())) {"),
        "unprotect_key_recheck":(
            "mm/filemap.c",
            "rcu_read_lock();\n#ifdef CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT",
            "#ifdef CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT"),
        "omit_dispatch_gate":(
            "mm/filemap.c",
            "if (!lockless_read_enabled() ||\n\t    !mapping_rcu_read(iocb->ki_filp->f_mapping))",
            "if (!mapping_rcu_read(iocb->ki_filp->f_mapping))"),
        "omit_mmap_safe_fallback":(
            "mm/filemap.c",
            "mapping_writably_mapped(mapping)) {\n\t\trcu_read_unlock();",
            "mapping_writably_mapped(mapping)) {"),
        "omit_writer_GP":(
            "mm/vma.c", "synchronize_rcu();","smp_mb();"),
        "omit_debugfs_disable":(
            "mm/filemap.c",
            "static_branch_disable(&lockless_read_key);",
            "/* disable key dropped */"),
        "remove_AS_RCU_READ_gate":(
            "include/linux/pagemap.h",
            "return test_bit(AS_RCU_READ, &mapping->flags);",
            "return true;"),
    }
    caught={}
    for label,(path,old,new) in mods.items():
        assert old in SRC[path],label
        clone=dict(SRC);clone[path]=clone[path].replace(old,new,1)
        failed=[k for k,v in gates(clone).items() if not v]
        assert failed,(label,"mutation escaped")
        caught[label]=failed
    return caught

def actual_herd():
    results={}
    for label,(litmus,outcome,pos,neg) in MODELS.items():
        p=K/"tools/memory-model/litmus-tests"/(litmus+".litmus")
        source=p.read_text()
        assert source.startswith("C "+litmus+"\n"),label
        assert ("r3=READ_ONCE(*active);" in source)==(label=="second_check_inside_rcu")
        proc=subprocess.run(
            ["herd7","-I","tools/memory-model","-conf","tools/memory-model/linux-kernel.cfg",
             str(p.relative_to(K))],cwd=K,
            text=True,capture_output=True,check=True,timeout=40)
        m=re.search(r"(?m)^Observation (\S+) (Never|Sometimes|Always) (\d+) (\d+)\s*$",proc.stdout)
        assert m and (m.group(1),m.group(2),int(m.group(3)),int(m.group(4)))==(
            litmus,outcome,pos,neg),(label,proc.stdout[-500:])
        normalized=re.sub(r"(?m)^Time [^\n]*\n?","",proc.stdout)
        results[label]={
            "name":litmus,"outcome":outcome,"positive":pos,"negative":neg,
            "source_sha256":hashlib.sha256(source.encode()).hexdigest(),
            "normalized_herd7_sha256":hashlib.sha256(normalized.encode()).hexdigest()
        }
    return results

def main():
    checks=gates(SRC)
    assert len(checks)==16,len(checks)
    assert all(checks.values()),[k for k,v in checks.items() if not v]
    mutated=real_source_mutations()
    assert len(mutated)==8
    models=actual_herd()
    assert models["old_key_check_outside_rcu"]["positive"]>0
    assert models["second_check_inside_rcu"]["positive"]==0
    assert OBJ.exists()
    fmt=subprocess.run(["file",str(OBJ)],text=True,capture_output=True,check=True,timeout=10).stdout
    assert "ELF 32-bit LSB relocatable, ARM, EABI5" in fmt,fmt
    obj_sha=hashlib.sha256(OBJ.read_bytes()).hexdigest()
    out={
      "stage":"G685", "scope":"ARM32 experiment-on reader-side key recheck source/LKMM",
      "source_guards":checks,"source_guards_pass":len(checks),
      "source_mutations_rejected":mutated,"actual_herd7":models,
      "experiment_on_ARM32_filemap_object":{
          "file":str(OBJ),"sha256":obj_sha,"format":fmt.strip()},
      "runtime_staged_new_code":"mm/filemap.c reader rechecks key under rcu_read_lock before accessing any unreferenced folio",
      "constraints":[
       "Current G683 ext4 mmap GP is still mandatory, including with runtime static-key OFF.",
       "Simple runtime OFF mmap GP shortcut remains unsafe without a writer drain/activation protocol.",
       "This catches one later-entering reader race in LKMM, but not every weak-memory stale load, all writer paths or CPU cache alias.",
       "No full linked kernel, no BOOT/TWRP flash or physical RCU ON test.",
       "S1 ACTIVE S2/S3 BLOCKED."
      ],
      "staged_runtime_files_not_installed":["mm/filemap.c","mm/page_alloc.c","mm/vma.c"]
    }
    OUT.write_text(json.dumps(out,indent=2,ensure_ascii=False)+"\n")
    print(f"G685 source {len(checks)}/16 PASS; {len(mutated)}/8 real source mutation rejects")
    print("G685 LKMM:",[(k,v["outcome"],v["positive"],v["negative"]) for k,v in models.items()])
    print("G685 ARM32 experiment-on mm/filemap.o:",obj_sha)

if __name__=="__main__":
    main()
