#!/usr/bin/env python3
"""G684: OFF->ON static-key transition vs ext4 writable mmap reservation.

Runs actual herd7 LKMM 3-actor litmus and checks source under the
SM-T311 ARMv7 configuration. A GP on the toggle thread *alone* cannot
serve as a publication GP for a writer that earlier observed OFF and
might still be publishing a VMA.
No executable patch, real PFN tracing, or in-device ON test.
"""
import hashlib
import json
import re
import subprocess
from pathlib import Path

ROOT=Path(__file__).resolve().parents[4]
CFG=ROOT.parent/"build/g633-armv7-rcu-zone-guard/.config"
FALSE_CFG=ROOT.parent/"build/g674-inode-aba-safety/.config"
EXPERIMENT_BUILD=ROOT.parent/"build/g633-armv7-rcu-zone-guard"
OUT=ROOT/"Documentation/mm/sm-t311-rcu-toggle-mmap-g684.json"
INPUTS=("mm/filemap.c","mm/vma.c","include/linux/jump_label.h",
        "kernel/rcu/tree.c","include/linux/fs.h")
SRC={key:(ROOT/key).read_text() for key in INPUTS}
TESTS={
  "skip_grace_if_runtime_OFF":("SMT311+ToggleOffWriterSkipG684","Sometimes",1,8),
  "each_writer_independent_GP":("SMT311+ToggleAllWritersGraceG684","Never",0,8),
  "skip_grace_with_memory_barrier":("SMT311+ToggleOffWriterBarrierG684","Sometimes",1,8),
}

def region(s,a,b):
    assert a in s,a
    tail=s.split(a,1)[1]
    assert b in tail,b
    return tail.split(b,1)[0]

def ordered(t,*xs):
    p=[t.find(x) for x in xs]
    return all(x>=0 for x in p) and p==sorted(p) and len(set(p))==len(p)

def checks(src,config):
    fm,vma,jump,rcu,fs=(src[x] for x in INPUTS)
    setkey=region(fm,"static int lockless_read_set(", "DEFINE_DEBUGFS_ATTRIBUTE(")
    batch=region(fm,"static bool filemap_read_rcu_batch(", "size_t filemap_read_rcu(")
    dispatch=region(fm,"size_t filemap_read_rcu(","ssize_t filemap_read(")
    writer=region(vma,"unsigned long mmap_region(","/**\n * do_brk_flags(")
    mmap=region(writer,"if (file && is_shared_maywrite(&vma_flags)) {",
                "writable_file_mapping = true;")
    mmapcode=re.sub(r"/\*.*?\*/","",mmap,flags=re.S)
    return {
      "defconfig_preempt_rcu":"CONFIG_PREEMPT_RCU=y" in config,
      "ARM32_TEST_CONFIG_EXP_ON":"CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y" in config,
      "ARM32_TEST_CONFIG_PAGE_RCU_FREE_ON":"CONFIG_PAGE_RCU_FREE=y" in config,
      "ARM32_TEST_CONFIG_DEBUGFS_ON":"CONFIG_DEBUG_FS=y" in config,
      "defconfig_no_jump_label":"# CONFIG_JUMP_LABEL is not set" in config,
      "no_jump_label_static_key_is_count_test":
          "#define static_branch_likely(x)" in jump and
          "likely_notrace(static_key_enabled(&(x)->key))" in jump,
      "SMT311_runtime_key_default_FALSE":
          "static DEFINE_STATIC_KEY_FALSE(lockless_read_key);" in fm,
      "debugfs_toggle_sets_reader_key":
          "static_branch_enable(&lockless_read_key);" in setkey and
          "static_branch_disable(&lockless_read_key);" in setkey,
      "debugfs_toggle_NOT_serialized_with_mmap_writer":
          "mapping_map_writable(" not in setkey and
          "mmap_lock(" not in setkey and "mutex_lock(" not in setkey,
      "reader_active_decision_before_enter_RCU":
          ordered(dispatch,"lockless_read_enabled()",
                  "filemap_read_rcu_batch(") and
          ordered(batch,"rcu_read_lock();","mapping_writably_mapped(mapping)",
                  "pagefault_disable();"),
      "writable_counter_file_shared":
          "atomic_inc_unless_negative(&mapping->i_mmap_writable)" in fs,
      "writable_count_published_before_writer_grace":
          ordered(mmap,"mapping_map_writable(file->f_mapping);",
                  "synchronize_rcu();"),
      "writer_GP_before_VMA_publication":
          ordered(writer,"synchronize_rcu();",
                  "writable_file_mapping = true;", "__mmap_region("),
      "writer_ext4_scope":
          "file->f_mapping->host->i_sb->s_magic == EXT4_SUPER_MAGIC" in mmap,
      "writer_does_NOT_observe_runtime_key_before_GP":
          "lockless_read_enabled(" not in mmapcode and
          "static_key_enabled(" not in mmapcode and
          "mapping_rcu_read(" not in mmapcode,
      "reader_has_no_global_serialize_mmap_lock":
          "mmap_read_lock(" not in batch and "mmap_write_lock(" not in batch,
      "runtime_key_toggle_does_not_wait_writers":
          "synchronize_rcu();" not in setkey and
          "wait_for_completion(" not in setkey,
      "RCU_synchronize_wait_contract_present":
          "void synchronize_rcu(void)" in rcu and
          "upon return from synchronize_rcu()" in rcu,
    }

def negative_controls(orig,config):
    mutations={
      "runtime_key_default_ON":("mm/filemap.c",
          "static DEFINE_STATIC_KEY_FALSE(lockless_read_key);",
          "static DEFINE_STATIC_KEY_TRUE(lockless_read_key);"),
      "toggle_no_enable":("mm/filemap.c",
          "static_branch_enable(&lockless_read_key);",
          "/* no runtime reader switch */"),
      "reader_before_RCU_count_check":("mm/filemap.c",
          "rcu_read_lock();\n#ifdef CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT",
          "#ifdef CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT"),
      "writer_skips_GP_if_key_OFF":("mm/vma.c",
          "if (file->f_mapping->host &&",
          "if (static_key_enabled(&lockless_read_key) && file->f_mapping->host &&"),
      "writer_omits_GP":("mm/vma.c",
          "synchronize_rcu();","smp_mb();"),
      "writer_publishes_before_GP":("mm/vma.c",
          "synchronize_rcu();","; /* GP omitted */"),
      "revert_writer_AS_flag_check":("mm/vma.c",
          "file->f_mapping->host->i_sb->s_magic == EXT4_SUPER_MAGIC",
          "mapping_rcu_read(file->f_mapping)"),
      "remove_shared_writable_counter":("mm/vma.c",
          "int error = mapping_map_writable(file->f_mapping);",
          "int error = 0;"),
    }
    caught={}
    for name,(filename,needle,replacement) in mutations.items():
        assert needle in orig[filename],name
        clone=dict(orig)
        clone[filename]=clone[filename].replace(needle,replacement,1)
        failing=[g for g,result in checks(clone,config).items() if not result]
        assert failing,(name,"not caught")
        caught[name]=failing
    return caught

def herd():
    results={}
    for key,(name,outcome,pos,neg) in TESTS.items():
        path=ROOT/"tools/memory-model/litmus-tests"/(name+".litmus")
        source=path.read_text()
        assert source.startswith("C "+name+"\n") and "P0(" in source and "P1(" in source and "P2(" in source
        assert "exists (0:r0=0" in source
        assert source.count("synchronize_rcu();")==2
        proc=subprocess.run(
            ["herd7","-I","tools/memory-model","-conf","tools/memory-model/linux-kernel.cfg",
             str(path.relative_to(ROOT))],
            cwd=ROOT,text=True,capture_output=True,timeout=55,check=True)
        m=re.search(r"(?m)^Observation (\S+) (Never|Sometimes|Always) (\d+) (\d+)\s*$",
                    proc.stdout)
        assert m,proc.stdout[-800:]
        assert (m.group(1),m.group(2),int(m.group(3)),int(m.group(4)))==(
            name,outcome,pos,neg),m.groups()
        normalized=re.sub(r"(?m)^Time [^\n]*\n?","",proc.stdout)
        results[key]={
            "model":str(path.relative_to(ROOT)),
            "observation":m.group(0),
            "litmus_sha256":hashlib.sha256(source.encode()).hexdigest(),
            "normalized_stdout_sha256":hashlib.sha256(normalized.encode()).hexdigest(),
            "outcome":outcome,"positive":pos,"negative":neg,
        }
    return results

def compile_evidence():
    objects={}
    for name in ("mm/vma.o","mm/filemap.o","mm/page_alloc.o"):
        p=EXPERIMENT_BUILD/name
        assert p.exists(),str(p)+" missing: build experiment ON first"
        hdr=subprocess.run(["file",str(p)],capture_output=True,text=True,timeout=8,check=True)
        assert "ELF 32-bit LSB relocatable, ARM, EABI5" in hdr.stdout,(name,hdr.stdout)
        objects[name]={
            "sha256":hashlib.sha256(p.read_bytes()).hexdigest(),
            "file":hdr.stdout.strip()
        }
    reloc=subprocess.run(["arm-linux-gnueabihf-readelf","-r",
        str(EXPERIMENT_BUILD/"mm/vma.o")],text=True,capture_output=True,check=True,timeout=12)
    assert "synchronize_rcu" in reloc.stdout,"GP branch absent from actual ARM32 compiled object"
    return objects

def main():
    config=CFG.read_text()
    false=FALSE_CFG.read_text()
    assert "CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y" not in false
    assert "# CONFIG_DEBUG_FS is not set" in false
    guards=checks(SRC,config)
    assert len(guards)==18,len(guards)
    assert all(guards.values()),[k for k,v in guards.items() if not v]
    mutants=negative_controls(SRC,config)
    assert len(mutants)==8
    models=herd()
    objects=compile_evidence()
    assert models["skip_grace_if_runtime_OFF"]["positive"]>0
    assert models["each_writer_independent_GP"]["positive"]==0
    assert models["skip_grace_with_memory_barrier"]["positive"]>0
    report={
        "stage":"G684", "kind":"Linux herdtools7 LKMM + source/config-anchored OFF->ON / mmap race gate",
        "source_predicates":guards, "source_predicates_pass":len(guards),
        "ARM32_experiment_ENABLED_objects":objects,
        "previous_false_config":"g674-inode-aba-safety had EXPERIMENT disabled: earlier mm/vma.o ARM32 check did NOT compile the guarded code",
        "active_test_config":"g633-armv7-rcu-zone-guard enables SM_T311_RCU_HOT4K_EXPERIMENT, PAGE_RCU_FREE and DEBUG_FS",
        "changed_source_negative_controls":mutants,
        "herd7_models":models,
        "finding":"Even adding a toggle-side RCU GP alone does not protect against an in-flight writer that saw OFF, skipped its own GP, and can publish a writable VMA after readers begin ON.",
        "consequence":"Cannot simply add if (!static_key_enabled(lockless_read_key)) skip GP to ext4 mmap. Current conservative G683 per-map GP remains STAGED while OFF overhead and activation proof are unresolved.",
        "limitations":[
            "Toy scalar key/count/VMA state with real LKMM, not actual mm_struct, static-key atomic API, or ARMv7 cache model.",
            "An allowed LKMM observation is an unsoundness witness for the naive gate, not evidence of physical UAF.",
            "Disallowing the condition when each writer waits is NOT a full safety proof.",
            "No cost or global lockdep proof and no physical RCU ON test.",
            "Full writer-drain/activation protocol must be proved before avoiding GP in OFF mode."
        ],
        "source_runtime_changes":"NONE",
        "staged_not_installed":["mm/filemap.c","mm/page_alloc.c","mm/vma.c"],
        "safety":{"S0":"PASS","S1":"ACTIVE","S2":"BLOCKED","S3":"BLOCKED",
                  "S4":"BLOCKED","S5":"BLOCKED","S6":"BLOCKED","S7":"PARTIAL","S8":"ACTIVE"},
    }
    OUT.write_text(json.dumps(report,indent=2,ensure_ascii=False)+"\n")
    print(f"G684 source {len(guards)}/18 PASS, changed source mutants {len(mutants)}/8 caught")
    print("G684 ARM32 actual experiment=y objects:",{k:v["sha256"] for k,v in objects.items()})
    print("G684 herd7:",[(k,v["outcome"],v["positive"],v["negative"]) for k,v in models.items()])
if __name__=="__main__":
    main()
