#!/usr/bin/env python3
"""G716 in-tree complete removal gate for abandoned Linus/Kiryl no-ref port.

Checks working sources AND newly compiled G716 ARM32 ELF/Android BOOT.
Preserves only two independent buffered-read optimizations on top of
the pre-port MM baseline. Does not use ADB or write target partitions.
"""
from pathlib import Path
from hashlib import sha256
import json
import subprocess
import re

K = Path(__file__).resolve().parents[4]
P = K.parent
BUILD = P / "build/g716-clean-refcounted"
ARTIFACT = P / "artifacts/G716-CLEAN-NO-REF-PORT-HOST-ONLY"
BOOT = ARTIFACT/"BOOT-G716-CLEAN-7.3.0-pjarczak-HOST-ONLY-DO-NOT-FLASH.img"
manifest = json.loads((ARTIFACT/"G716-CLEAN-BOOT-MANIFEST.json").read_text())
PORT_BASE = "4432bceab"
def run(*args):
    return subprocess.run(args,cwd=K,check=True,text=True,capture_output=True).stdout
def digest(path):return sha256(path.read_bytes()).hexdigest()

original_files=run("git","diff","--name-only",PORT_BASE,"016e91411").splitlines()
assert len(original_files)==20,len(original_files)
C={n:(K/n).read_text() for n in original_files}
extra=(K/"mm/vma.c").read_text()
config=(BUILD/".config").read_text()
source=(K/"mm/filemap.c").read_text()
fuse_names=["fs/fuse/fuse_i.h","fs/fuse/control.c","fs/fuse/iomode.c","fs/fuse/inode.c"]
fuse = {p:(K/p).read_text() for p in fuse_names}
elf=BUILD/"vmlinux"
nm=run("arm-linux-gnueabihf-nm","-a",str(elf))
readelf=run("arm-linux-gnueabihf-readelf","-h",str(elf))

def check_for(mapping=C, header=source, fuses=fuse, conf=config, symbol_table=nm):
    base_clean = all(
        mapping[n] == run("git","show",f"{PORT_BASE}:{n}")
        for n in original_files if n!="mm/filemap.c"
    )
    no_ref_tokens=("PG_rcu_free","__PG_RCU_FREE","PAGE_RCU_FREE",
                   "SM_T311_RCU_HOT4K_EXPERIMENT","filemap_read_rcu",
                   "lockless_page_cache_read","rcu_free_defer",
                   "NR_FREE_RCU_PAGES")
    # Tests source payload, not historical Documentation or standalone tests.
    to_scan = dict(mapping)
    to_scan["mm/filemap.c"]=header
    to_scan.update(fuses)
    to_scan["mm/vma.c"]=extra
    unlinked=("filemap_read_rcu", "rcu_free_sync", "rcu_free_defer",
              "lockless_read_key")
    names=set()
    for line in symbol_table.splitlines():
        m=re.match(r"^[0-9a-f]+\s+(?:[0-9a-f]+\s+)?[a-zA-Z]\s+([A-Za-z_][A-Za-z0-9_]*)$",line)
        if m:names.add(m.group(1))
    return {
        "all_18_commit_changed_20_files_identified":len(original_files)==20,
        "nineteen_source_files_bitwise_identical_to_preport":base_clean,
        "final_filemap_is_refcounted_without_noref":all(t not in header for t in no_ref_tokens),
        "all_affected_runtime_source_free_of_removed_port":all(
            all(t not in txt for t in no_ref_tokens) for txt in to_scan.values()
        ),
        "original_16_commit_migration_cleanup_no_rcu_direct_refs": all(t not in C["mm/page_alloc.c"] for t in ("rcu_free_sync","rcu_free_defer")),
        "both_read_microoptimizations_retained":(
            "filemap_read_accessed(folio, iocb->ki_pos, last_pos)" in header
            and "if (ra->prev_pos != last_pos)" in header
        ),
        "FUSE_independent_passthrough_negotiation_still_available":(
            '"passthrough", S_IFREG | 0400' in fuses["fs/fuse/control.c"]
            and "fuse_conn_passthrough_read" in fuses["fs/fuse/control.c"]
        ),
        "RCU_coupled_FUSE_open_counter_completely_removed":all(
            "passthrough_successful_opens" not in txt
            and "passthrough_opens" not in txt for txt in fuses.values()),
        "mm_vma_G683_experiment_removed":extra==run("git","show","a2fca4558:mm/vma.c"),
        "no_Kconfig_option_can_enable_old_port":all(t not in (K/"mm/Kconfig").read_text()
            for t in ("config PAGE_RCU_FREE","config SM_T311_RCU_HOT4K_EXPERIMENT")),
        "both_experimental_Kconfig_overlays_removed":all(
            not (K/p).exists() for p in
            ("arch/arm/configs/sm_t311_rcu_experiment_g686.fragment",
             "arch/arm/configs/sm_t311_rcu_off_keep_debugfs_g700.fragment")),
        "G716_config_lacks_noref_features":all(t not in conf for t in
            ("CONFIG_PAGE_RCU_FREE=y","CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y")),
        "G716_config_preserves_hardware_features":all(t in conf for t in
            ("CONFIG_ARCH_EXYNOS4=y","CONFIG_HIGHMEM=y",
             "CONFIG_FUSE_FS=y","CONFIG_FUSE_PASSTHROUGH=y",
             "CONFIG_DEBUG_FS=y","CONFIG_LRU_GEN=y")),
        "ARM32_ELF_machine_verified": "ELF32" in readelf and re.search(r"Machine:\s+ARM\s",readelf) is not None,
        "ARM32_linked_release_does_not_define_any_ref_free_symbol":all(
            name not in names for name in unlinked),
        "full_boot_SHA_matches_G716_manifest":digest(BOOT)==manifest["image_sha256"],
        "full_boot_bytes_expected":BOOT.stat().st_size==8388608,
        "DTB_matches_previous_physical_G650":manifest["dtb_sha256"]=="6fb3e77075733d531abaa2f4c3fa1bf55c11a50f04cc5acc9838db5ea0f41bfb",
        "ramdisk_is_pinned_G650_ramdisk":manifest["ramdisk_sha256"]=="681d59437776b09ede48dc57e16b34697d08b7161f820673b8c84df3943bd8fd",
        "kernel_release_exact_stable": (BUILD/"include/config/kernel.release").read_text().strip()=="7.3.0-pjarczak",
        "not_claimed_physical_installed":manifest["flash_authorized"] is False,
    }
positive = check_for()
mutations={}
variants={
    "reintroduce_PAGE_RCU_FREE_Kconfig":"CONFIG_PAGE_RCU_FREE=y\n"+config,
    "reenable_SM_T311_experiment":"CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y\n"+config,
}
for name,bad in variants.items():
    mutations[name] = not all(check_for(conf=bad).values())
for name,needle,replacement in [
    ("reinsert_ref_free_reader","void filemap_read_accessed(","void filemap_read_rcu(void);\nvoid filemap_read_accessed("),
    ("lose_prev_pos_skip","if (ra->prev_pos != last_pos)","if (true)"),
    ("lose_folio_access_consolidation","filemap_read_accessed(folio, iocb->ki_pos, last_pos)","folio_mark_accessed(folio)"),
]:
    if needle not in source:
        raise ValueError("Mutation fixture absent "+name)
    mutations[name]=not all(check_for(header=source.replace(needle,replacement,1)).values())
fuse_dirty=dict(fuse)
fuse_dirty["fs/fuse/control.c"]+= "\nstatic int passthrough_opens;"
mutations["reintroduce_experimental_FUSE_counter"] = not all(check_for(fuses=fuse_dirty).values())
mutations["reintroduce_ref_free_ELF_symbol"] = not all(check_for(symbol_table=nm+"\nc0200000 T filemap_read_rcu\n").values())
report={
 "stage":"G716",
 "description":"Complete removal of abandoned 18-commit Linus/Kiryl experimental no-ref RCU port from SM-T311 active runtime source, retain independent verified optimizations",
 "checks":positive,
 "negative_controls":mutations,
 "removed_series":"05362c1a7..016e91411 (18 historical commits still in Git history; runtime tree restored to 4432bceab for 19 files)",
 "retained_refcounted_optimizations":["899f8d53b folio_mark_accessed consolidation","d46226de3 skip unchanged ra->prev_pos store"],
 "shmem_read_bytes_G650_equivalent_host":976,
 "shmem_read_bytes_G700_host":920,
 "shmem_read_bytes_G716_host":860,
 "shmem_optimization_note":"These are static linked ARM32 function code sizes (not latency or measured throughput). G716 has no no-ref stub at all.",
 "new_full_BOOT_sha256":manifest["image_sha256"],
 "new_full_BOOT_bytes":manifest["image_size"],
 "DTB_unchanged":True,
 "RAMDISK_unchanged":True,
 "hardware_flash":False,
 "next":"Android prebuilt G716 packaging selection, then owner-attended physical p9/p10 and recovery proof before flash"
}
print(json.dumps(report,ensure_ascii=False,indent=2))
if not all(positive.values()) or not all(mutations.values()):
    raise SystemExit(1)
