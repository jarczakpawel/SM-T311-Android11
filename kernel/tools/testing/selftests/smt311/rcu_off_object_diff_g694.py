#!/usr/bin/env python3
"""G694: independent matched ARM32 G650 vs current RCU-OFF object comparison.

No speed claim: opcode count and R_ARM_CALL elimination are *static*
compiler evidence, not measured device throughput or power. Rebuild old
G650 from exact pinned git commit into independent O= tree as noted
in accompanying RST if baseline object files are absent.
"""
from pathlib import Path
import subprocess,re,hashlib,json

ROOT=Path(__file__).resolve().parents[4]
OLD=ROOT.parent/"comparison/g694-g650-build"
NEW=ROOT.parent/"build/g687-release-rcu-off"
OUTPUT=ROOT/"Documentation/mm/sm-t311-rcu-off-object-diff-g694.json"
FUNCTIONS={"mm/filemap.o":"filemap_read",
           "mm/shmem.o":"shmem_file_read_iter"}
G650="a2fca4558"

def run(*args):
    return subprocess.run(list(args),cwd=ROOT,text=True,capture_output=True,
                          timeout=40,check=True).stdout

def objdata(root,object_name,symbol):
    path=root/object_name
    assert path.exists(),path
    meta=run("arm-linux-gnueabihf-nm","-S",str(path))
    match=re.search(
        r"(?m)^([0-9a-f]+) ([0-9a-f]+) [Tt] "+re.escape(symbol)+r"$",
        meta)
    assert match,(path,symbol)
    start,size=[int(x,16) for x in match.groups()]
    disasm=run("arm-linux-gnueabihf-objdump","-dr",
               "--disassemble="+symbol,str(path))
    opcodes=re.findall(r"(?m)^\s*[0-9a-f]+:\s+([0-9a-f]{8})\s",disasm)
    relocations=re.findall(r"(?m)^\s*[0-9a-f]+:\s+(R_ARM_\w+)\s+(\S+)",
                           disasm)
    return {
       "elf_sha256":hashlib.sha256(path.read_bytes()).hexdigest(),
       "size_bytes":size,
       "section_address":start,
       "opcode_count":len(opcodes),
       "opcodes":opcodes,
       "relocations":relocations
    }

def gates(base,rel,config_match,compiler_match,source):
    old_file=base["mm/filemap.o"];new_file=rel["mm/filemap.o"]
    old_shmem=base["mm/shmem.o"];new_shmem=rel["mm/shmem.o"]
    def named(calls,name):
        return sum(symbol==name for reloc,symbol in calls)
    return {
       "old_source_provenance_is_G650":run("git","cat-file","-t",G650).strip()=="commit",
       "exact_matching_config":config_match,
       "exact_matching_compiler":compiler_match,
       "release_uses_RCU_OFF":
         "CONFIG_PAGE_RCU_FREE=y" not in (NEW/".config").read_text() and
         "CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y" not in (NEW/".config").read_text(),
       "same_UTS_RELEASE_both":
         all((p/"include/config/kernel.release").read_text().strip()=="7.3.0-pjarczak"
             for p in (OLD,NEW)),
       "baseline_filemap_no_RCU_stub_call":
         named(old_file["relocations"],"filemap_read_rcu")==0,
       "release_filemap_no_RCU_stub_call":
         named(new_file["relocations"],"filemap_read_rcu")==0,
       "filemap_identical_machine_instruction_words":
         old_file["opcodes"]==new_file["opcodes"],
       "filemap_no_instruction_reduction":
         old_file["opcode_count"]==new_file["opcode_count"]==309,
       "G650_shmem_did_call_noop_stub":
         named(old_shmem["relocations"],"filemap_read_rcu")==1,
       "G694_release_shmem_no_stub_call":
         named(new_shmem["relocations"],"filemap_read_rcu")==0,
       "shmem_instructions_reduced_by_14":
         old_shmem["opcode_count"]-new_shmem["opcode_count"]==14,
       "shmem_code_bytes_reduced_by_56":
         old_shmem["size_bytes"]-new_shmem["size_bytes"]==56,
       "source_shmem_only_REMOVED_stub_call_when_PAGE_RCU_FREE_OFF":
         "#elif defined(CONFIG_PAGE_RCU_FREE)\n\tretval = filemap_read_rcu(iocb, to, last_pos);" in source and
         "retval = 0;\n#endif\n\tif (retval)" in source,
       "release_shmem_copy_path_still_uses_folio_reference":
         named(new_shmem["relocations"],"folio_unlock")>=1 and
         named(new_shmem["relocations"],"copy_page_to_iter")>=1,
       "release_pagecache_read_still_gets_referenced_folios":
         named(new_file["relocations"],"copy_page_to_iter")>=1 and
         named(new_file["relocations"],"__folio_put")>=1,
       "G692_failclosed_still_present":
         "if (val)\n\t\treturn -EOPNOTSUPP;" in (ROOT/"mm/filemap.c").read_text(),
    }

def mutated_cases(baseline,release,match_config,match_compiler,source):
    changes={
      "invent_G687_speedup":lambda a,b,c,d,s: (
          {**a,"mm/filemap.o":{**a["mm/filemap.o"],
              "opcodes":["e3a00000"]}},b,c,d,s),
      "delete_G688_benefit":lambda a,b,c,d,s: (
          a,{**b,"mm/shmem.o":{**b["mm/shmem.o"],
              "opcode_count":a["mm/shmem.o"]["opcode_count"],
              "size_bytes":a["mm/shmem.o"]["size_bytes"]}},c,d,s),
      "reintroduce_G650_shmem_stub":lambda a,b,c,d,s: (
          a,{**b,"mm/shmem.o":{**b["mm/shmem.o"],
              "relocations":b["mm/shmem.o"]["relocations"]+[
                    ("R_ARM_CALL","filemap_read_rcu")]}},c,d,s),
      "mismatched_kernel_config":lambda a,b,c,d,s:(a,b,False,d,s),
      "mismatched_toolchain":lambda a,b,c,d,s:(a,b,c,False,s),
      "remove_SOURCE_release_guard":lambda a,b,c,d,s:(
           a,b,c,d,s.replace("retval = filemap_read_rcu(iocb, to, last_pos);",
                             "retval = -EFAULT;")),
      "remove_folio_put_runtime_call":lambda a,b,c,d,s:(
           a,{**b,"mm/filemap.o":{**b["mm/filemap.o"],
              "relocations":[(k,n) for k,n in b["mm/filemap.o"]["relocations"]
                             if n!="__folio_put"]}},c,d,s),
    }
    caught={}
    for name,fn in changes.items():
        modified=fn(baseline,release,match_config,match_compiler,source)
        failed=[k for k,v in gates(*modified).items() if not v]
        assert failed,name+" escaped"
        caught[name]=failed
    return caught

def main():
    assert (OLD/".config").exists(),"G650 independent ARM32 baseline not compiled"
    assert (NEW/".config").exists()
    config_match=(OLD/".config").read_bytes()==(NEW/".config").read_bytes()
    def compiler(root):
        return run("arm-linux-gnueabihf-readelf","-p",".comment",
                   str(root/"mm/filemap.o")).split("String dump",1)[-1]
    compiler_match=compiler(OLD)==compiler(NEW) and "GCC: (Debian 16.2.0-3) 16.2.0" in compiler(NEW)
    old={};rel={}
    for file,symbol in FUNCTIONS.items():
        old[file]=objdata(OLD,file,symbol)
        rel[file]=objdata(NEW,file,symbol)
    source=(ROOT/"mm/shmem.c").read_text()
    positive=gates(old,rel,config_match,compiler_match,source)
    assert len(positive)==17 and all(positive.values()),positive
    negative=mutated_cases(old,rel,config_match,compiler_match,source)
    assert len(negative)==7
    def slim(row):
        return {k:v for k,v in row.items() if k!="opcodes"}
    report={
       "stage":"G694","baseline_git_commit":G650,
       "target_last_executable_source_commit":run(
           "git","log","-1","--format=%h","--","mm/filemap.c",
           "mm/page_alloc.c","mm/shmem.c","mm/vma.c").strip(),
       "real_independent_ARM32_object_build":True,
       "compiler_same":compiler_match,"config_byte_identical":config_match,
       "source_and_machine_code_gates":positive,
       "source_gates_passed":len(positive),
       "mutated_real_comparison_inputs_detected":negative,
       "objects":{"g650":{k:slim(v) for k,v in old.items()},
                  "release_rcu_off":{k:slim(v) for k,v in rel.items()}},
       "findings":[
         "filemap_read: 309 identical ARM instruction words at G650 and current RCU OFF; no extra compiled speed gain from G687 despite cleaner C.",
         "shmem_file_read_iter: G650 had 244 instructions and R_ARM_CALL filemap_read_rcu; current RCU OFF has 230 and zero such calls, 56 bytes smaller after G688.",
         "Opcode/instruction counts are static. This is NOT measured throughput, power, cache misses or latency on physical SM-T311.",
         "Neither host object nor host zImage is Samsung boot.img; physical BOOT remains G650.",
       ],
       "safety":"G692 experimental RCU ON fail-closed remains -EOPNOTSUPP, no new runtime C change",
       "staged_runtime_files_NOT_INSTALLED":[
          "mm/filemap.c","mm/page_alloc.c","mm/shmem.c","mm/vma.c"]
    }
    OUTPUT.write_text(json.dumps(report,indent=2,ensure_ascii=False)+"\n")
    print("G694",len(positive),"/17 ARM32 source+binary assertions PASS,",
          len(negative),"/7 tampered controls REJECTED")
    print("filemap_read:",old["mm/filemap.o"]["opcode_count"],"->",
          rel["mm/filemap.o"]["opcode_count"],"IDENTICAL WORDS")
    print("shmem_file_read_iter:",old["mm/shmem.o"]["size_bytes"],"->",
          rel["mm/shmem.o"]["size_bytes"],"bytes;",
          old["mm/shmem.o"]["opcode_count"],"->",
          rel["mm/shmem.o"]["opcode_count"],"instructions")
if __name__=="__main__":
    main()
