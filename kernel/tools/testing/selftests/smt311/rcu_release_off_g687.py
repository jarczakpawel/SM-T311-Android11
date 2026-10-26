#!/usr/bin/env python3
"""G687 independent ARM32 vanilla SM-T311 release OFF versus G686 experiment ON.

Requires independently compiled ../build/g687-release-rcu-off from the
tracked sm_t311_android11_defconfig, with LOCALVERSION= to pin UTS_RELEASE.
No writes outside in-tree JSON, no ADB/flash/BOOT repack.
Checks that PAGE_RCU_FREE, SM_T311_RCU_HOT4K_EXPERIMENT are absent from
the linked release ELF, and that the experiment-specific mmap
synchronize_rcu is not present in release mm/vma.o.
"""
from pathlib import Path
import hashlib
import json
import re
import subprocess

ROOT=Path(__file__).resolve().parents[4]
OFF=ROOT.parent/"build/g687-release-rcu-off"
ON=ROOT.parent/"build/g686-reproducible-exp-on"
OUT=ROOT/"Documentation/mm/sm-t311-rcu-release-off-g687.json"
BASE=ROOT/"arch/arm/configs/sm_t311_android11_defconfig"
FRAGMENT=ROOT/"arch/arm/configs/sm_t311_rcu_experiment_g686.fragment"
DEBUGFS_OFF_FRAGMENT=ROOT/"arch/arm/configs/sm_t311_rcu_off_keep_debugfs_g700.fragment"
RELEASE="7.3.0-pjarczak"
SYMBOLS=("filemap_read_rcu","lockless_read_key","lockless_read_set",
         "mmap_region","synchronize_rcu")
RUNTIME=["mm/filemap.c","mm/page_alloc.c","mm/shmem.c","mm/vma.c"]


def command(argv):
    return subprocess.run(argv,cwd=ROOT,text=True,capture_output=True,
                          timeout=40,check=True).stdout


def checks(off, on, base, fragment, off_nm, on_nm, off_vma_reloc, on_vma_reloc,
           release, on_release, diffs):
    def symbol(table,name):
        return re.search(r"(?m)^[0-9a-fA-F]+ [A-Za-z] "+re.escape(name)+r"$",table)!=None
    return {
       "tracked_default_base_does_not_enable_experiment":
           "CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y" not in base
           and "CONFIG_PAGE_RCU_FREE=y" not in base
           and "# CONFIG_DEBUG_FS is not set" in base,
       "tracked_overlay_opt_in_only":
           "CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y" in fragment
           and "CONFIG_DEBUG_FS=y" in fragment
           and "CONFIG_PAGE_RCU_FREE=y" in fragment,
       "release_experiment_disabled":
           "CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y" not in off,
       "release_PAGE_RCU_FREE_disabled":
           "CONFIG_PAGE_RCU_FREE=y" not in off,
       "release_DEBUG_FS_disabled":
           "# CONFIG_DEBUG_FS is not set" in off,
       "experiment_CONFIG_compiled_not_runtime_ON":
           "CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y" in on
           and "CONFIG_PAGE_RCU_FREE=y" in on
           and "CONFIG_DEBUG_FS=y" in on,
       "release_has_zero_return_reader_stub_but_no_runtime_key":
           symbol(off_nm,"filemap_read_rcu")
           and not symbol(off_nm,"lockless_read_key"),
       "runtime_key_storage_absent_from_release":
           not symbol(off_nm,"lockless_read_key"),
       "runtime_toggler_absent_from_release":
           not symbol(off_nm,"lockless_read_set"),
       "experimental_symbols_linked_in_comparator":
           all(symbol(on_nm,n) for n in SYMBOLS),
       "ext4_mmap_GP_specific_call_absent_in_release_vma":
           "synchronize_rcu" not in off_vma_reloc,
       "ext4_mmap_GP_call_present_in_experimental_vma":
           "synchronize_rcu" in on_vma_reloc,
       "mmap_region_production_symbol_retained":
           symbol(off_nm,"mmap_region"),
       "exact_release_uts_both":
           release.strip()==RELEASE and on_release.strip()==RELEASE,
       "only_four_modified_runtime_paths_and_two_overlays":
           diffs == sorted(RUNTIME+[str(FRAGMENT.relative_to(ROOT)),
                                    str(DEBUGFS_OFF_FRAGMENT.relative_to(ROOT))]),
       "release_has_no_unsafe_fastpath_globally":
           "CONFIG_PAGE_RCU_FREE=y" not in off and
           "CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y" not in off
           and not symbol(off_nm,"lockless_read_key")
           and not symbol(off_nm,"lockless_read_set")
    }


def mutants(args):
    off,on,base,frag,off_nm,on_nm,off_vma,on_vma,off_release,on_release,diffs=args
    cases={
      "accidentally_enable_experiment":(off+"\nCONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y",on,base,frag,off_nm,on_nm,off_vma,on_vma,off_release,on_release,diffs),
      "accidentally_enable_PAGE_RCU_FREE":(off+"\nCONFIG_PAGE_RCU_FREE=y",on,base,frag,off_nm,on_nm,off_vma,on_vma,off_release,on_release,diffs),
      "release_name_plus":(off,on,base,frag,off_nm,on_nm,off_vma,on_vma,RELEASE+"+",on_release,diffs),
      "delete_release_reader_stub":(
          off,on,base,frag,re.sub(r"(?m)^.* filemap_read_rcu$", "", off_nm),
          on_nm,off_vma,on_vma,off_release,on_release,diffs),
      "reintroduce_unref_toggle":(off,on,base,frag,off_nm+"\nc123abce t lockless_read_set",on_nm,off_vma,on_vma,off_release,on_release,diffs),
      "reintroduce_GP_under_mmap":(off,on,base,frag,off_nm,on_nm,off_vma+"\nR_ARM_CALL synchronize_rcu",on_vma,off_release,on_release,diffs),
      "drop_GP_from_exp_comparator":(off,on,base,frag,off_nm,on_nm,off_vma,"",off_release,on_release,diffs),
      "untracked_fifth_kernel_runtime_path":(off,on,base,frag,off_nm,on_nm,off_vma,on_vma,off_release,on_release,sorted(diffs+["mm/memory.c"])),
    }
    caught={}
    for name,args_ in cases.items():
        failures=[k for k,v in checks(*args_).items() if not v]
        assert failures,(name,"mutant escaped")
        caught[name]=failures
    return caught


def artifact(build,relative):
    p=build/relative
    assert p.exists(),p
    data=p.read_bytes()
    fmt=command(["file",str(p)]).strip()
    if relative=="arch/arm/boot/zImage":
        assert "Linux kernel ARM boot executable zImage" in fmt,fmt
    else:
        assert "ELF 32-bit LSB" in fmt and "ARM, EABI5" in fmt,fmt
    return {"sha256":hashlib.sha256(data).hexdigest(),"bytes":len(data),
            "file":fmt,"host_path":str(p)}


def main():
    off=(OFF/".config").read_text()
    on=(ON/".config").read_text()
    base=BASE.read_text()
    overlay=FRAGMENT.read_text()
    off_nm=command(["arm-linux-gnueabihf-nm",str(OFF/"vmlinux")])
    on_nm=command(["arm-linux-gnueabihf-nm",str(ON/"vmlinux")])
    off_reloc=command(["arm-linux-gnueabihf-readelf","-r",str(OFF/"mm/vma.o")])
    on_reloc=command(["arm-linux-gnueabihf-readelf","-r",str(ON/"mm/vma.o")])
    release=(OFF/"include/config/kernel.release").read_text()
    on_release=(ON/"include/config/kernel.release").read_text()
    diff=sorted(command(["git","diff","--name-only","a2fca4558","HEAD",
                         "--","mm","fs","kernel","drivers","arch","include","init"]).splitlines())
    args=(off,on,base,overlay,off_nm,on_nm,off_reloc,on_reloc,release,on_release,diff)
    results=checks(*args)
    assert len(results)==16,len(results)
    assert all(results.values()),[k for k,v in results.items() if not v]
    negatives=mutants(args)
    assert len(negatives)==8
    # The upstream-linked filemap_read_rcu symbol is a two-instruction
    # zero-return stub under ARM32 CONFIG_PAGE_RCU_FREE=n, not a live
    # reference-free folio reader. The old unconditional caller DID
    # call this stub; the G687 no-PAGE_RCU_FREE branch skips that call
    # in mm/filemap.c:filemap_read while preserving experimental readers.
    off_stub=command(["arm-linux-gnueabihf-objdump","-d",
                      "--disassemble=filemap_read_rcu",str(OFF/"vmlinux")])
    off_caller=command(["arm-linux-gnueabihf-objdump","-d",
                        "--disassemble=filemap_read",str(OFF/"vmlinux")])
    on_caller=command(["arm-linux-gnueabihf-objdump","-d",
                       "--disassemble=filemap_read",str(ON/"vmlinux")])
    shmem_caller=command(["arm-linux-gnueabihf-objdump","-d",
                          "--disassemble=shmem_file_read_iter",str(OFF/"vmlinux")])
    src=(ROOT/"mm/filemap.c").read_text()
    assembly_tests={
       "ARM32_release_stub_mov_zero":
           re.search(r"(?m)^.*\tmov\s+r0, #0\s*$",off_stub) is not None,
       "ARM32_release_stub_bx_lr":
           re.search(r"(?m)^.*\tbx\s+lr\s*$",off_stub) is not None,
       "release_hot_filemap_read_call_eliminated":
           re.search(r"\bbl\s+.*<filemap_read_rcu>",off_caller) is None,
       "experiment_hot_filemap_read_call_preserved":
           re.search(r"\bbl\s+.*<filemap_read_rcu>",on_caller) is not None,
       "release_source_explicit_CONFIG_PAGE_RCU_FREE_guard":
           "#elif defined(CONFIG_PAGE_RCU_FREE)\n\tfast_read = filemap_read_rcu(iocb, iter, last_pos);\n#else" in src,
       "release_source_fallback_constant_zero":
           "fast_read = 0;\n#endif\n\tif (fast_read)" in src,
       "G688_release_shmem_stub_call_removed":
           re.search(r"\bbl\s+.*<filemap_read_rcu>",shmem_caller) is None,
    }
    assert len(assembly_tests)==7,len(assembly_tests)
    assert all(assembly_tests.values()),[k for k,v in assembly_tests.items() if not v]
    # Test the decision rules against actual modified disassembly inputs.
    disasm_mutants={
       "reintroduce_release_filemap_stub_call": re.search(
           r"\bbl\s+.*<filemap_read_rcu>",
           off_caller+"\n c0100000: eb000000 bl c0111111 <filemap_read_rcu>") is not None,
       "remove_experiment_filemap_call": re.search(
           r"\bbl\s+.*<filemap_read_rcu>",
           re.sub(r"(?m)^.*\bbl\s+.*<filemap_read_rcu>.*$", "",on_caller)) is None,
       "corrupt_release_stub_zero_return": re.search(
           r"(?m)^.*\tmov\s+r0, #0\s*$",off_stub.replace("mov\tr0, #0","mov\tr0, #1")) is None,
    }
    assert all(disasm_mutants.values()),disasm_mutants
    files={}
    for label,root in (("release_rcu_off",OFF),("experimental_ON_compiled_but_G692_activation_refused",ON)):
        files[label]={}
        for rel in ("vmlinux","arch/arm/boot/zImage","mm/vma.o","mm/filemap.o"):
            files[label][rel]=artifact(root,rel)
        hdr=(root/"include/generated/utsrelease.h").read_text().strip()
        assert hdr==f'#define UTS_RELEASE "{RELEASE}"',(label,hdr)
    report={
       "stage":"G687",
       "result":"HOST COMPLETE RELEASE OFF LINK AND EXPERIMENT ON-CAPABLE COMPARATOR; NO BOOT/FLASH",
       "last_executable_source_commit":command([
           "git","log","-1","--format=%h","--",*RUNTIME]).strip(),
       "kernel_release":RELEASE,
       "positive_source_and_binary_guards":results,
       "ARMv7_callsite_assembly_gates":assembly_tests,
       "ARMv7_callsite_modified_disassembly_negative_controls":disasm_mutants,
       "positive_guards_pass":len(results),
       "tampered_input_controls_rejected":negatives,
       "source_tree_changes":["arch/arm/configs/sm_t311_rcu_experiment_g686.fragment",
                              "arch/arm/configs/sm_t311_rcu_off_keep_debugfs_g700.fragment"]+RUNTIME,
       "host_linked_artifacts":files,
       "limitations":[
         "Configuration OFF proves experiment source excluded by Kconfig, but not overall correctness of unrelated kernel changes or physical performance.",
         "No RCU ON physical trial; no statement that new zImage is flashable Samsung BOOT.",
         "Neither build has CONFIG_PROVE_LOCKING enabled: no measured lockdep graph.",
         "No actual Cortex-A9 p99 mmap latency, cache alias/HighMem lifetime or PFN ABA proof.",
         "The experimental build still adds unconditional ext4 VM_SHARED|MAYWRITE synchronize_rcu under mmap_lock EVEN if its runtime key OFF; release OFF build does not."
       ],
       "physical_kernel":"G650, unchanged",
       "experiment_staged_not_installed":RUNTIME,
    }
    OUT.write_text(json.dumps(report,indent=2,ensure_ascii=False)+"\n")
    print("G687 release OFF / experiment ON-capable",len(results),"/16 PASS;",
          len(negatives),"/8 changed-input controls REJECTED")
    print("G687",len(assembly_tests),"/7 ARM32 disassembly checks PASS,",
          len(disasm_mutants),"/3 modified-disassembly controls REJECTED")
    for label,things in files.items():
        for name,data in things.items():
            print(label,name,data["bytes"],data["sha256"])

if __name__=="__main__":
    main()
