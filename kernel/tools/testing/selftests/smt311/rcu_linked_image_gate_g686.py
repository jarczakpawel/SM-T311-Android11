#!/usr/bin/env python3
"""G686: validate the actual linked ARMv7 experiment-on zImage and version.

Run ONLY after:
 make -j6 O=../build/g633-armv7-rcu-zone-guard ARCH=arm \
      CROSS_COMPILE=arm-linux-gnueabihf- LOCALVERSION= zImage

This gate never flashes, invokes adb, packages boot partitions or makes backups.
It verifies Kconfig, UTS naming, ELF architecture, physical object files,
symbol inclusion and exact stage-vs-physical executable source diff.
"""
from pathlib import Path
import hashlib
import os
import json
import re
import subprocess

SRC = Path(__file__).resolve().parents[4]
BUILD = (SRC / os.environ.get(
    "SMT311_BUILD_DIR", "../build/g686-reproducible-exp-on")).resolve()
assert BUILD.is_relative_to((SRC.parent / "build").resolve()), BUILD
OUT = SRC / "Documentation/mm/sm-t311-rcu-linked-image-g686.json"
PHYSICAL_SOURCE = "a2fca4558"
EXPECT_SOURCE_DIFF = ["mm/filemap.c", "mm/page_alloc.c", "mm/shmem.c", "mm/vma.c"]
FRAGMENT = SRC / "arch/arm/configs/sm_t311_rcu_experiment_g686.fragment"
G700_FRAGMENT = SRC / "arch/arm/configs/sm_t311_rcu_off_keep_debugfs_g700.fragment"
REQUIRED = ["CONFIG_ARCH_EXYNOS4=y", "CONFIG_SMP=y", "CONFIG_HIGHMEM=y",
            "CONFIG_DEBUG_FS=y", "CONFIG_PAGE_RCU_FREE=y",
            "CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y",
            "CONFIG_PREEMPT_RCU=y",
            'CONFIG_LOCALVERSION="-pjarczak"',
            "# CONFIG_LOCALVERSION_AUTO is not set"]
EXACT_RELEASE = "7.3.0-pjarczak"
REQUIRED_LINKED_SYMBOLS = ("filemap_read_rcu", "lockless_read_set",
                           "lockless_read_key", "mmap_region", "synchronize_rcu")
ARTIFACTS = ("vmlinux", "arch/arm/boot/zImage",
             "mm/filemap.o", "mm/page_alloc.o", "mm/vma.o")


def out(cmd, *, cwd=SRC):
    return subprocess.run(cmd, cwd=cwd, capture_output=True,
                          text=True, timeout=45, check=True).stdout


def attest(config, release, uts, symbols, names):
    tests = {
        "experiment_truly_enabled": all(x in config.splitlines() for x in REQUIRED),
        "exact_release_no_plus": release.strip() == EXACT_RELEASE,
        "exact_UTS_RELEASE": uts.strip() == f'#define UTS_RELEASE "{EXACT_RELEASE}"',
        "all_linked_core_symbols": all(
            re.search(r"(?m)^[0-9a-fA-F]+ [A-Za-z] "+re.escape(sym)+r"$",
                      symbols) is not None for sym in REQUIRED_LINKED_SYMBOLS),
        "exact_four_staged_runtime_paths": names == EXPECT_SOURCE_DIFF,
    }
    return tests


def mutants(config, release, uts, symbols, names):
    # Real inputs are altered in memory and must FAIL the same production gates.
    cases = {
        "accidental_plus_suffix":(config, EXACT_RELEASE+"+", uts, symbols, names),
        "UTS_unpinned":(config, release, '#define UTS_RELEASE "7.3.0-pjarczak+"', symbols, names),
        "experiment_compiled_off":(
            config.replace("CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y",
                           "# CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT is not set"),
            release, uts, symbols, names),
        "page_rcu_free_disabled":(
            config.replace("CONFIG_PAGE_RCU_FREE=y",
                           "# CONFIG_PAGE_RCU_FREE is not set"),
            release, uts, symbols, names),
        "unlinked_filemap_fastpath":(
            config,release,uts,
            re.sub(r"(?m)^.* filemap_read_rcu$","",symbols),names),
        "fifth_accidental_runtime_path":(
            config,release,uts,symbols,names+["mm/memory.c"]),
        "lost_mmap_gp_linked_symbol":(
            config,release,uts,
            re.sub(r"(?m)^.* synchronize_rcu$","",symbols),names),
    }
    rejected = {}
    for case,args in cases.items():
        wrong = attest(*args)
        problems = [k for k,v in wrong.items() if not v]
        assert problems,(case,"mutation escaped")
        rejected[case] = problems
    return rejected


def main():
    config = (BUILD / ".config").read_text()
    fragment = FRAGMENT.read_text()
    assert all(x in fragment.splitlines() for x in (
        "CONFIG_DEBUG_FS=y","CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y",
        "CONFIG_PAGE_RCU_FREE=y",'CONFIG_LOCALVERSION="-pjarczak"'))
    release = (BUILD / "include/config/kernel.release").read_text()
    uts = (BUILD / "include/generated/utsrelease.h").read_text()
    symbols = out(["arm-linux-gnueabihf-nm",str(BUILD/"vmlinux")])
    all_changed = out(["git","diff","--name-only",PHYSICAL_SOURCE,"HEAD",
                   "--","mm","fs","kernel","drivers","arch","include","init"]).splitlines()
    allowed_configs = [str(FRAGMENT.relative_to(SRC)),
                       str(G700_FRAGMENT.relative_to(SRC))]
    assert all(all_changed.count(x)==1 for x in allowed_configs),all_changed
    alternate = G700_FRAGMENT.read_text()
    assert "CONFIG_DEBUG_FS=y" in alternate
    assert "# CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT is not set" in alternate
    assert "# CONFIG_PAGE_RCU_FREE is not set" in alternate
    changed = [p for p in all_changed if p not in allowed_configs]
    checks = attest(config,release,uts,symbols,changed)
    assert len(checks)==5
    assert all(checks.values()),[k for k,v in checks.items() if not v]
    rejections=mutants(config,release,uts,symbols,changed)
    artifacts={}
    for rel in ARTIFACTS:
        p=BUILD / rel
        assert p.is_file(),rel
        data=p.read_bytes()
        fmt=out(["file",str(p)]).strip()
        if rel=="arch/arm/boot/zImage":
            assert "Linux kernel ARM boot executable zImage" in fmt,fmt
            assert len(data)>2_000_000
        else:
            assert ("ELF 32-bit LSB" in fmt and "ARM, EABI5" in fmt),fmt
        artifacts[rel]={"bytes":len(data),"sha256":hashlib.sha256(data).hexdigest(),
                        "format":fmt}
    elf_header = out(["arm-linux-gnueabihf-readelf","-h",str(BUILD/"vmlinux")])
    assert "Machine:                           ARM" in elf_header,elf_header
    assert "Class:                             ELF32" in elf_header
    # Detect stale binaries and distinguish runtime-source provenance from
    # test/doc-only commits, which must not invalidate a verified build.
    rt_dirty=out(["git","status","--porcelain","--"]+EXPECT_SOURCE_DIFF)
    assert not rt_dirty.strip(),"staged runtime sources modified after build: "+rt_dirty
    freshness={}
    linked_time=(BUILD/"vmlinux").stat().st_mtime_ns
    zipped_time=(BUILD/"arch/arm/boot/zImage").stat().st_mtime_ns
    for source_file in EXPECT_SOURCE_DIFF:
        obj="mm/"+Path(source_file).stem+".o"
        obj_path=BUILD/obj
        fresh=obj_path.stat().st_mtime_ns>= (SRC/source_file).stat().st_mtime_ns
        assert fresh,f"outdated ARM32 object {obj}"
        linkfresh=linked_time>=obj_path.stat().st_mtime_ns
        assert linkfresh,f"outdated linked vmlinux for {obj}"
        freshness[obj]=bool(fresh and linkfresh)
    assert zipped_time>=linked_time,"outdated zImage relative to vmlinux"
    runtime_source_commit=out(["git","log","-1","--format=%h","--"]+
                              EXPECT_SOURCE_DIFF).strip()
    info={
      "stage":"G686","result":"HOST FULL LINK PASS, NOT PHYSICALLY BOOT-TESTED",
      "last_runtime_source_commit":runtime_source_commit,
      "tracked_experiment_config_fragment":"arch/arm/configs/sm_t311_rcu_experiment_g686.fragment",
      "tracked_fragment_sha256":hashlib.sha256(fragment.encode()).hexdigest(),
      "runtime_source_objects_fresh_in_linked_image":freshness,
      "booted_physical_kernel_source":PHYSICAL_SOURCE,
      "kernel_release":release.strip(),
      "config_checks":checks,
      "changed_input_negative_controls":rejections,
      "exact_staged_runtime_paths_NOT_INSTALLED":changed,
      "non_runtime_source_tree_differences":allowed_configs,
      "linked_symbols_verified":list(REQUIRED_LINKED_SYMBOLS),
      "output_artifacts":artifacts,
      "command":"bash tools/testing/selftests/smt311/build_verify_no_flash_g686.sh",
      "warnings":[
         "LOCALVERSION= must be supplied even with CONFIG_LOCALVERSION_AUTO=n, otherwise Kbuild appends a + when source Git is not an exact annotated kernel version tag.",
         "This image was only built on host. It is NOT a Samsung BOOT image, TWRP OTA or a physical boot test.",
         "RCU reader/writer correctness, global mmap_lock lockdep, PFN/ABA and ARMv7 cache coherence remain unproven. Do NOT flash G686 as RCU ON.",
         "The staged experiment's ext4 shared-maywrite mmap grace cost still occurs even with runtime key OFF.",
      ]
    }
    OUT.write_text(json.dumps(info,indent=2,ensure_ascii=False)+"\n")
    print("G686 FULL ARM32 LINK",release.strip())
    print("G686",len(checks),"/5 attestation checks PASS;",
          len(rejections),"/7 negative controls REJECTED")
    for name,artifact in artifacts.items():
        print(name,artifact["sha256"],artifact["bytes"])
    print("G686 STAGED NOT INSTALLED:",", ".join(changed))


if __name__=="__main__":
    main()
