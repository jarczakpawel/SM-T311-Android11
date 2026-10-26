#!/usr/bin/env python3
"""G695: physically measured /proc/config.gz provenance and matched ARM32 code.

This is a *static* comparison of a physical G650 source config reconstructed
from a measured /proc/config.gz fingerprint. No physical benchmark/flash.
"""
from pathlib import Path
import hashlib,json,re,subprocess

ROOT=Path(__file__).resolve().parents[4]
PROFILE=ROOT/"Documentation/mm/sm-t311-physical-profile-g695.json"
OUT=ROOT/"Documentation/mm/sm-t311-physical-binary-g695.json"
BASE=ROOT.parent/"comparison/g695-g650-experiment-build"
EXP=ROOT.parent/"build/g686-reproducible-exp-on"
OFF=ROOT.parent/"build/g687-release-rcu-off"
FILES={"mm/filemap.o":["filemap_read","filemap_read_rcu","lockless_read_set"],
       "mm/shmem.o":["shmem_file_read_iter"],
       "mm/page_alloc.o":["rcu_free_sync"],
       "mm/vma.o":["mmap_region"]}
BASE_COMMIT="a2fca4558"

def command(*args):
    return subprocess.run(args,cwd=ROOT,text=True,capture_output=True,
                          check=True,timeout=38).stdout

def details(build,rel,name):
    path=build/rel
    assert path.is_file(),path
    nm=command("arm-linux-gnueabihf-nm","-S",str(path))
    m=re.search(r"(?m)^[0-9a-f]+ ([0-9a-f]+) [Tt] "+re.escape(name)+r"$",nm)
    if not m:return {"present":False}
    dis=command("arm-linux-gnueabihf-objdump","-dr",
                "--disassemble="+name,str(path))
    op=re.findall(r"(?m)^\s*[0-9a-f]+:\s+([0-9a-f]{8})\s+",dis)
    calls=re.findall(r"(?m)^\s*[0-9a-f]+:\s+R_ARM_(?:CALL|JUMP24)\s+(\S+)",dis)
    return {"present":True,"size_bytes":int(m.group(1),16),
            "instructions":len(op),"opcodes":op,"calls":calls}

def compare(obs,profiles,config_hash_match,g650_experimental_match,cmp_compiler,source):
    g=profiles["g650_experimental"]
    e=profiles["new_experimental_on_refused"]
    off=profiles["new_release_off"]
    key=lambda tree,file,name:tree[file][name]
    bm=key(g,"mm/vma.o","mmap_region")
    em=key(e,"mm/vma.o","mmap_region")
    om=key(off,"mm/vma.o","mmap_region")
    br=key(g,"mm/filemap.o","filemap_read")
    er=key(e,"mm/filemap.o","filemap_read")
    sr=key(g,"mm/shmem.o","shmem_file_read_iter")
    se=key(e,"mm/shmem.o","shmem_file_read_iter")
    so=key(off,"mm/shmem.o","shmem_file_read_iter")
    bf=key(g,"mm/page_alloc.o","rcu_free_sync")
    ef=key(e,"mm/page_alloc.o","rcu_free_sync")
    bt=key(g,"mm/filemap.o","lockless_read_set")
    et=key(e,"mm/filemap.o","lockless_read_set")
    return {
      "real_6794_line_physical_config_hash_reconstructed":config_hash_match,
      "G650_host_experiment_same_dotconfig":g650_experimental_match,
      "all_3_built_with_same_ARM_compiler":cmp_compiler,
      "physical_boot_experiment_options_enabled":
          all(opt in (BASE/".config").read_text() for opt in obs["enabled"]),
      "release_off_config_excludes_experiment":
          "CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y" not in (OFF/".config").read_text()
          and "CONFIG_PAGE_RCU_FREE=y" not in (OFF/".config").read_text(),
      "baseline_G650_commit_grounded":
          command("git","cat-file","-t",BASE_COMMIT).strip()=="commit",
      "G650_experiment_filemap_RCU_call_still_compiled":
          br["calls"].count("filemap_read_rcu")==1,
      "new_experiment_filemap_RCU_call_still_compiled":
          er["calls"].count("filemap_read_rcu")==1,
      "experiment_shmem_equals_G650_machine_words":
          sr["opcodes"]==se["opcodes"] and sr["instructions"]==230,
      "experiment_shmem_EQUALS_release_machine_words":
          sr["opcodes"]==so["opcodes"] and sr["size_bytes"]==920,
      "G650_experiment_mmap_did_NOT_GP":
          bm["calls"].count("synchronize_rcu")==0 and bm["instructions"]==89,
      "current_experiment_mmap_GP_call_PRESENT_even_key_disabled":
          em["calls"].count("synchronize_rcu")==1 and em["instructions"]==100
          and "synchronize_rcu();" in source,
      "normal_release_off_mmap_matches_G650_experiment":
          bm["opcodes"]==om["opcodes"] and om["instructions"]==89
          and "synchronize_rcu" not in om["calls"],
      "experiment_GP_added_11_arm_instructions":
          em["instructions"]-bm["instructions"]==11,
      "base_rcu_free_sync_flush_work_twice":
          bf["calls"].count("flush_work")==2 and bf["size_bytes"]==96,
      "new_rcu_free_sync_flush_rcu_work_once":
          ef["calls"].count("flush_rcu_work")==1 and
          ef["calls"].count("flush_work")==1 and ef["size_bytes"]==96,
      "experiment_setter_g692_source_failclosed":
          "if (val)\n\t\treturn -EOPNOTSUPP;" in
          (ROOT/"mm/filemap.c").read_text(),
      "experiment_setter_shrunk_to_failclosed":
          bt["instructions"]==132 and et["instructions"]==55,
      "physical_debugfs_runtime_key_NOT_directly_proven":
          "could not be read" in " ".join(obs["safe_interpretation"]),
    }

def mutate_compare(obs,trees,hashok,configok,compilerok,source):
    variations={
       "wrong_device_config_SHA": lambda o,t,h,c,cc,s:(o,t,False,c,cc,s),
       "wrong_build_config":lambda o,t,h,c,cc,s:(o,t,h,False,cc,s),
       "wrong_compiler":lambda o,t,h,c,cc,s:(o,t,h,c,False,s),
       "fictional_speedup_filemap":lambda o,t,h,c,cc,s:
           (o,{**t,"g650_experimental":{**t["g650_experimental"],
               "mm/filemap.o":{**t["g650_experimental"]["mm/filemap.o"],
                  "filemap_read":{**t["g650_experimental"]["mm/filemap.o"]["filemap_read"],
                                "calls":[]}}}},h,c,cc,s),
       "erase_added_mmap_GP":lambda o,t,h,c,cc,s:
           (o,{**t,"new_experimental_on_refused":{
                **t["new_experimental_on_refused"],
                "mm/vma.o":{**t["new_experimental_on_refused"]["mm/vma.o"],
                 "mmap_region":{**t["new_experimental_on_refused"]["mm/vma.o"]["mmap_region"],
                     "calls":[]}}}},h,c,cc,s),
       "pretend_release_has_GP":lambda o,t,h,c,cc,s:
           (o,{**t,"new_release_off":{**t["new_release_off"],
             "mm/vma.o":{**t["new_release_off"]["mm/vma.o"],
               "mmap_region":{**t["new_release_off"]["mm/vma.o"]["mmap_region"],
                    "calls":["synchronize_rcu"]}}}},h,c,cc,s),
       "break_release_shmem":lambda o,t,h,c,cc,s:
           (o,{**t,"new_release_off":{**t["new_release_off"],
             "mm/shmem.o":{**t["new_release_off"]["mm/shmem.o"],
               "shmem_file_read_iter":{**t["new_release_off"]["mm/shmem.o"]["shmem_file_read_iter"],
                   "opcodes":["00000000"]}}}},h,c,cc,s),
       "reintroduce_wrong_RCU_sync":lambda o,t,h,c,cc,s:
           (o,{**t,"new_experimental_on_refused":{**t["new_experimental_on_refused"],
             "mm/page_alloc.o":{**t["new_experimental_on_refused"]["mm/page_alloc.o"],
               "rcu_free_sync":{**t["new_experimental_on_refused"]["mm/page_alloc.o"]["rcu_free_sync"],
                  "calls":["flush_work","flush_work"]}}}},h,c,cc,s),
       "remove_fail_closed":lambda o,t,h,c,cc,s:
           (o,t,h,c,cc,s.replace("synchronize_rcu();","other_barrier();")),
    }
    captured={}
    for name,fn in variations.items():
        args=fn(obs,trees,hashok,configok,compilerok,source)
        fail=[k for k,v in compare(*args).items() if not v]
        assert fail,(name,"counterfactual falsely passed")
        captured[name]=fail
    return captured

def main():
    observed=json.loads(PROFILE.read_text())
    cfg=(EXP/".config").read_text()
    assert len(observed["physical_config_differences_from_host_experiment"])==3
    modified=cfg
    for diff in observed["physical_config_differences_from_host_experiment"]:
        assert modified.count(diff["host"])==1
        modified=modified.replace(diff["host"],diff["physical"],1)
    hash_match=hashlib.sha256(modified.encode()).hexdigest()==observed["physical_uncompressed_config_sha256"]
    config_match=(BASE/".config").read_bytes()==(EXP/".config").read_bytes()
    compiler=[]
    for root in (BASE,EXP,OFF):
        txt=command("arm-linux-gnueabihf-readelf","-p",".comment",
                    str(root/"mm/filemap.o"))
        compiler.append("GCC: (Debian 16.2.0-3) 16.2.0" in txt)
    compiler_match=all(compiler)
    builds={"g650_experimental":BASE,"new_experimental_on_refused":EXP,
            "new_release_off":OFF}
    data={label:{f:{n:details(root,f,n) for n in names}
                 for f,names in FILES.items()} for label,root in builds.items()}
    source=(ROOT/"mm/vma.c").read_text()
    positives=compare(observed,data,hash_match,config_match,compiler_match,source)
    assert len(positives)==19 and all(positives.values()),positives
    mutants=mutate_compare(observed,data,hash_match,config_match,compiler_match,source)
    assert len(mutants)==9
    def minimal(d):
        return {name:{file:{symbol:{k:v for k,v in x.items() if k!="opcodes"}
                  for symbol,x in sym.items()} for file,sym in obj.items()}
                for name,obj in d.items()}
    report={
      "stage":"G695",
      "source":"G650 physical /proc/config.gz, independent G650 source objects, current experimental ON-refused objects and release-OFF objects",
      "physical_config":observed,
      "guards":positives,"positive_guard_count":len(positives),
      "modified_binary_or_metadata_inputs_rejected":mutants,
      "compiled_sources":minimal(data),
      "decision":[
         "PHYSICAL G650 has CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y, CONFIG_PAGE_RCU_FREE=y and CONFIG_DEBUG_FS=y. Default release OFF is NOT its compiled configuration.",
         "Direct physical config is 6794 lines; reconstruction from full experimental host .config differs only in three Rust toolchain metadata entries; SHA256 matches the running device exactly.",
         "Compiling the G650 source under matching experimental config proves the added mmap_region -> synchronize_rcu() branch occurs only in CURRENT experimental comparator even though new G692 ON is fail-closed.",
         "This added GP could add mmap latency if installed; no real physical runtime cost has been measured.",
         "Current experimental setter refuses ON (G692); the physical G650 setter does not include the G692 interlock. Config y alone does NOT prove the physical key is ON; debugfs read was inaccessible as shell.",
         "RCU-OFF source-only comparison G694 is still valid for matched host builds but MUST NOT be called physical G650 BOOT-versus-new BOOT benchmark.",
         "No executable code change or device flash in G695."
      ],
      "staged_runtime_executable_paths":["mm/filemap.c","mm/page_alloc.c","mm/shmem.c","mm/vma.c"]
    }
    OUT.write_text(json.dumps(report,indent=2,ensure_ascii=False)+"\n")
    print(f"G695 {len(positives)}/19 physical config+ARM32 guards PASS; {len(mutants)}/9 mutated controls REJECTED")
    print("physical reconstructed SHA match",hash_match)
    for key in ("G650_experiment_mmap_did_NOT_GP","current_experiment_mmap_GP_call_PRESENT_even_key_disabled",
                "normal_release_off_mmap_matches_G650_experiment"):
        print(key,positives[key])

if __name__=="__main__": main()
