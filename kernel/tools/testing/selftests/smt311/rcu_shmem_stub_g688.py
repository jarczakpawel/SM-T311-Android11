#!/usr/bin/env python3
"""G688: prove the empty RCU read stub call was removed from ARMv7 shmem.

Check actual linked ARM32 release OFF and experiment-enabled outputs;
stage only code changes, no ADB/BOOT/flash. Earlier G687 removed the
same wasted call in filemap_read, but shmem_file_read_iter kept it.
"""
from pathlib import Path
import hashlib, json, re, subprocess
K=Path(__file__).resolve().parents[4]
OFF=K.parent/"build/g687-release-rcu-off"
ON=K.parent/"build/g686-reproducible-exp-on"
OUT=K/"Documentation/mm/sm-t311-rcu-shmem-stub-g688.json"
SHMEM=K/"mm/shmem.c"
FILEMAP=K/"mm/filemap.c"

def run(cmd):
    return subprocess.run(cmd,cwd=K,capture_output=True,text=True,check=True,timeout=48).stdout

def disasm(build,symbol):
    return run(["arm-linux-gnueabihf-objdump","-d",
                "--disassemble="+symbol,str(build/"vmlinux")])

def source_gates(shmem,filemap,config_off,config_on):
    before=run(["git","show","9e374ff1b:mm/shmem.c"])
    shared=shmem.split("static ssize_t shmem_file_read_iter(",1)[1].split("for (;;) {",1)[0]
    return {
      "original_release_shmem_stub_call_existed":
          "#else\n\tretval = filemap_read_rcu(iocb, to, last_pos);\n#endif" in before,
      "G688_release_guard_has_constant_zero":
          "#elif defined(CONFIG_PAGE_RCU_FREE)\n\tretval = filemap_read_rcu(iocb, to, last_pos);\n#else" in shared
          and "retval = 0;\n#endif" in shared,
      "experiment_always_falls_back_for_shmem":
          "#ifdef CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT\n\tretval = 0;" in shared,
      "keep_upstream_ref_free_route_when_available":
          "defined(CONFIG_PAGE_RCU_FREE)" in shared and
          "retval = filemap_read_rcu(iocb, to, last_pos);" in shared,
      "ordinary_filemap_G687_opt_retained":
          "#elif defined(CONFIG_PAGE_RCU_FREE)\n\tfast_read = filemap_read_rcu(iocb, iter, last_pos);\n#else" in filemap,
      "release_opted_out_PAGE_RCU_FREE":
          "CONFIG_PAGE_RCU_FREE=y" not in config_off and
          "CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y" not in config_off,
      "experimental_overlay_opted_in":
          "CONFIG_PAGE_RCU_FREE=y" in config_on and
          "CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y" in config_on,
      "kernel_release_pinned":
          (OFF/"include/config/kernel.release").read_text().strip()=="7.3.0-pjarczak" and
          (ON/"include/config/kernel.release").read_text().strip()=="7.3.0-pjarczak"
    }

def source_mutants(shmem,filemap,cfg_off,cfg_on):
    substitutions={
      "restore_original_shmem_stub":(
        "shmem", "#elif defined(CONFIG_PAGE_RCU_FREE)\n\tretval = filemap_read_rcu(iocb, to, last_pos);\n#else\n",
        "#else\n"),
      "remove_available_feature_shmem_call":(
        "shmem","retval = filemap_read_rcu(iocb, to, last_pos);",
        "retval = 0;"),
      "remove_experimental_shmem_guard":(
        "shmem","#ifdef CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT\n\tretval = 0;",
        "#ifdef CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT\n\tretval = filemap_read_rcu(iocb, to, last_pos);"),
      "remove_filemap_G687_release_guard":(
        "filemap","#elif defined(CONFIG_PAGE_RCU_FREE)\n\tfast_read = filemap_read_rcu(iocb, iter, last_pos);",
        "#else\n\tfast_read = filemap_read_rcu(iocb, iter, last_pos);"),
      "release_feature_accidentally_on":(
        "config_off","# CONFIG_DEBUG_FS is not set",
        "# CONFIG_DEBUG_FS is not set\nCONFIG_PAGE_RCU_FREE=y"),
      "experimental_feature_accidentally_off":(
        "config_on","CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y",
        "# CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT is not set")
    }
    rejected={}
    raw={"shmem":shmem,"filemap":filemap,"config_off":cfg_off,"config_on":cfg_on}
    for name,(file,old,new) in substitutions.items():
        assert old in raw[file],name
        changed=dict(raw);changed[file]=changed[file].replace(old,new,1)
        failures=[k for k,v in source_gates(
            changed["shmem"],changed["filemap"],
            changed["config_off"],changed["config_on"]).items() if not v]
        assert failures,(name,"source mutant escaped")
        rejected[name]=failures
    return rejected

def main():
    shmem=SHMEM.read_text()
    fm=FILEMAP.read_text()
    off_cfg=(OFF/".config").read_text()
    on_cfg=(ON/".config").read_text()
    predicates=source_gates(shmem,fm,off_cfg,on_cfg)
    assert len(predicates)==8 and all(predicates.values()),predicates
    negatives=source_mutants(shmem,fm,off_cfg,on_cfg)
    assert len(negatives)==6
    release_caller=disasm(OFF,"shmem_file_read_iter")
    experiment_caller=disasm(ON,"shmem_file_read_iter")
    release_reader=disasm(OFF,"filemap_read")
    experiment_reader=disasm(ON,"filemap_read")
    release_stub=disasm(OFF,"filemap_read_rcu")
    has_call=lambda s: re.search(r"\bbl\s+[^\n]*<filemap_read_rcu>",s) is not None
    assembly={
      "shmem_release_no_stub_call":not has_call(release_caller),
      "shmem_experiment_no_ext4_only_reader_call":not has_call(experiment_caller),
      "filemap_release_no_stub_call":not has_call(release_reader),
      "filemap_experiment_still_calls_reader":has_call(experiment_reader),
      "release_rcu_stub_returns_zero":
          bool(re.search(r"\bmov\s+r0, #0",release_stub)
               and re.search(r"\bbx\s+lr",release_stub)),
      "filemap_rcu_key_absent_from_release":
          not re.search(r"(?m)^\w+\s+\w\s+lockless_read_key$",run([
              "arm-linux-gnueabihf-nm",str(OFF/"vmlinux")])),
    }
    assert len(assembly)==6 and all(assembly.values()),assembly
    mutations={
       "inject_stub_call_into_release_shmem_detected":
          has_call(release_caller+"\nc0001010: eb000000 bl c0010000 <filemap_read_rcu>"),
       "missing_expected_experiment_filemap_call_detected":
          not has_call(re.sub(r"(?m)^.*\bbl\s+[^\n]*<filemap_read_rcu>.*$","",experiment_reader)),
    }
    assert all(mutations.values()),mutations
    hashes={}
    for name,root in (("release_OFF",OFF),("experiment_test",ON)):
        hashes[name]={}
        for rel in ("vmlinux","arch/arm/boot/zImage","mm/shmem.o"):
            file=root/rel
            assert file.is_file(),str(file)
            fmt=run(["file",str(file)])
            assert "ARM" in fmt,fmt
            hashes[name][rel]={"sha256":hashlib.sha256(file.read_bytes()).hexdigest(),
                               "bytes":file.stat().st_size}
        assert (root/"vmlinux").stat().st_mtime_ns >= (root/"mm/shmem.o").stat().st_mtime_ns
        assert (root/"arch/arm/boot/zImage").stat().st_mtime_ns >= (root/"vmlinux").stat().st_mtime_ns
    report={
      "stage":"G688",
      "result":"ARM32 linked release OFF shmem stub call eliminated; experiment path preserved; HOST ONLY",
      "source_gates":predicates,"source_gates_pass":len(predicates),
      "changed_source_mutants_rejected":negatives,
      "linked_ARMv7_disassembly_checks":assembly,
      "linked_disassembly_negative_controls":mutations,
      "linked_object_hashes":hashes,
      "source_runtime_paths_STAGED_NOT_INSTALLED":["mm/filemap.c","mm/page_alloc.c","mm/shmem.c","mm/vma.c"],
      "limits":[
        "No Cortex-A9 measured benchmark: assembly call removal does not quantify speedup.",
        "No change to experimental RCU safety; S2/S3 BLOCKED and no physical RCU ON.",
        "Release stub symbol still linked for other users; shmem/filemap callers shown skip it.",
        "Experimental ext4 mmap GP still expensive even at key OFF; host-only kernels are not Samsung BOOT images.",
      ]
    }
    OUT.write_text(json.dumps(report,indent=2,ensure_ascii=False)+"\n")
    print("G688",len(predicates),"/8 source PASS;",len(negatives),
          "/6 changed C sources rejected;",len(assembly),
          "/6 real ARM32 disassembly PASS;",len(mutations),"/2 counterfactual ASM rejected")
    for name,things in hashes.items():
        for f,v in things.items():print(name,f,v["sha256"])

if __name__=="__main__":
    main()
