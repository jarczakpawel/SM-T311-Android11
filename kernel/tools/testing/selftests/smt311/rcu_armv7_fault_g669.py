#!/usr/bin/env python3
"""G669: true ARMv7 shared-file fault and PTE cache sync source gate.

This source-level audit does NOT prove a runtime wrong-byte read, ARMv7
cache alias failure or RCU-safe object lifetime. Mutations rerun the same
checks on deliberately altered source inputs.
"""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
PATHS = (
    "arch/arm/mm/mmu.c", "arch/arm/mm/flush.c",
    "arch/arm/mm/mmap.c", "arch/arm/mm/fault-armv.c",
    "arch/arm/include/asm/tlbflush.h",
    "arch/arm/include/asm/pgtable.h", "arch/arm/include/asm/cachetype.h",
    "arch/arm/configs/sm_t311_android11_defconfig",
    "mm/memory.c", "mm/filemap.c", "mm/vma.c", "fs/ext4/file.c",
    "fs/ext4/inode.c",
)
SOURCES = {path: (ROOT / path).read_text() for path in PATHS}
OUT = ROOT / "Documentation/mm/sm-t311-rcu-armv7-mmap-fault-g669.json"


def segment(source, begin, end):
    assert begin in source and end in source, (begin, end)
    return source.split(begin, 1)[1].split(end, 1)[0]


def ordered(source, *tokens):
    return all(x in source for x in tokens) and all(
        source.index(tokens[i]) < source.index(tokens[i+1])
        for i in range(len(tokens)-1)
    )


def checks(src):
    mmu = src["arch/arm/mm/mmu.c"]
    arm = src["arch/arm/mm/flush.c"]
    ammap = src["arch/arm/mm/mmap.c"]
    tlb = src["arch/arm/include/asm/tlbflush.h"]
    pgtable = src["arch/arm/include/asm/pgtable.h"]
    ctype = src["arch/arm/include/asm/cachetype.h"]
    defconfig = src["arch/arm/configs/sm_t311_android11_defconfig"]
    memory = src["mm/memory.c"]
    fm = src["mm/filemap.c"]
    vma = src["mm/vma.c"]
    ext4 = src["fs/ext4/file.c"]
    extinode = src["fs/ext4/inode.c"]

    pte = segment(mmu, "void set_ptes(", "\n}\n")
    sync = segment(arm, "void __sync_icache_dcache(", "\n}\n")
    armflush = segment(arm, "void __flush_dcache_folio(", "\n}\n")
    cachepages = segment(arm, "void flush_cache_pages(", "\n}\n")
    armmap = segment(ammap, "arch_get_unmapped_area(", "\n}\n")
    shared = segment(memory, "static vm_fault_t do_shared_fault(", "\n}\n")
    finish = segment(memory, "void set_pte_range(", "\n}\n")
    reuse = segment(memory, "static inline void wp_page_reuse(", "\n}\n")
    extmkw = segment(extinode, "vm_fault_t ext4_page_mkwrite(", "\n}\n")
    read = segment(fm, "static bool filemap_read_rcu_batch(", "\n}\n")
    mregion = segment(vma, "unsigned long mmap_region(", "\n}\n")
    return {
        "board_config_vipt": "CONFIG_CPU_CACHE_VIPT=y" in defconfig,
        "board_config_smp": "CONFIG_SMP=y" in defconfig,
        "arm_pte_checks_valid_user": "pte_valid_user(pteval)" in pte,
        "arm_pte_avoids_special_sync": "if (!pte_special(pteval))" in pte,
        "arm_pte_sync_before_publish": ordered(
            pte, "__sync_icache_dcache(pteval);", "set_pte_ext(ptep, pteval, ext);"),
        "armv7_update_mmu_cache_is_noop": (
            "#if __LINUX_ARM_ARCH__ < 6" in tlb and
            "static inline void update_mmu_cache_range(" in tlb),
        "armv7_real_sync_defined": "#if __LINUX_ARM_ARCH__ >= 6" in arm and
                                    "void __sync_icache_dcache(pte_t pteval)" in arm,
        "arm_alias_selected_by_runtime_cacheid": (
            "cache_is_vipt_aliasing()" in sync and
            "mapping = folio_flush_mapping(folio);" in sync),
        "arm_sync_flushes_dirty_kernel_alias": (
            "if (!test_bit(PG_dcache_clean, &folio->flags.f))" in sync and
            "__flush_dcache_folio(mapping, folio);" in sync),
        "arm_nonalias_nonexec_skips_flush": (
            "cache_is_vipt_nonaliasing() && !pte_exec(pteval)" in sync),
        "arm_flush_lowmem_direct_map": (
            "!folio_test_highmem(folio)" in armflush and
            "__cpuc_flush_dcache_area(folio_address(folio)" in armflush),
        "arm_flush_alias_colour": (
            "cache_is_vipt_aliasing()" in armflush and
            "flush_pfn_alias(folio_pfn(folio), folio_pos(folio))" in armflush),
        "arm_flush_cachepage_alias": (
            "cache_is_vipt_aliasing()" in cachepages and
            "flush_pfn_alias(pfn, user_addr)" in cachepages),
        "arm_mmap_file_colour_alignment": (
            "do_align = filp || (flags & MAP_SHARED)" in armmap and
            "info.align_offset = pgoff << PAGE_SHIFT" in armmap),
        "arm_fixed_shared_alias_rejection": (
            "if (aliasing && flags & MAP_SHARED &&" in armmap and
            "return -EINVAL;" in armmap),
        "file_ext4_mkwrite_hook": ".page_mkwrite   = ext4_page_mkwrite" in ext4,
        "mm_shared_fault_mkwrite_before_pte": ordered(
            shared, "do_page_mkwrite(vmf, folio);", "finish_fault(vmf);"),
        "mm_set_pte_uses_set_ptes": (
            "set_ptes(vma->vm_mm, addr, vmf->pte, entry, nr);" in finish),
        "mm_fault_reuse_flushes_before_marking_writable": ordered(
            reuse, "flush_cache_page(vma, vmf->address, pte_pfn(vmf->orig_pte));",
            "maybe_mkwrite(pte_mkdirty(entry), vma);"),
        "ext4_mkwrite_takes_shared_invalidate": (
            "filemap_invalidate_lock_shared(mapping);" in extmkw),
        "ext4_mkwrite_has_folio_identity_check": "folio->mapping != mapping" in extmkw,
        "mmap_counter_inc_precedes_region": (
            "mapping_map_writable(file->f_mapping)" in mregion and
            ordered(mregion, "mapping_map_writable(file->f_mapping)",
                    "__mmap_region(file, addr, len, vma_flags, pgoff, uf);")),
        "noref_counter_guard_precedes_rcu": ordered(
            read, "mapping_writably_mapped(mapping)", "rcu_read_lock();",
            "filemap_read_rcu_copy("),
        "noref_copy_lacks_mmap_lock": (
            "i_mmap_lock_read(" not in read and "i_mmap_lock_write(" not in read),
    }


def main():
    valid = checks(SOURCES)
    assert len(valid) == 24, len(valid)
    missing = [k for k, v in valid.items() if not v]
    if missing:
        raise SystemExit("G669 SOURCE GATE FAIL: " + ", ".join(missing))

    mutants = {
        "pte_flush_deleted": ("arch/arm/mm/mmu.c",
                              "__sync_icache_dcache(pteval);"),
        "arm_dcache_clean_flush_deleted": ("arch/arm/mm/flush.c",
                                         "__flush_dcache_folio(mapping, folio);"),
        "arm_colour_alias_deleted": ("arch/arm/mm/flush.c",
                                    "flush_pfn_alias(folio_pfn(folio), folio_pos(folio));"),
        "shared_fault_page_mkwrite_deleted": (
            "mm/memory.c", "tmp = do_page_mkwrite(vmf, folio);"),
        "arm_map_fixed_colour_check_deleted": (
            "arch/arm/mm/mmap.c", "if (aliasing && flags & MAP_SHARED &&"),
        "reader_mmap_guard_deleted": (
            "mm/filemap.c", "mapping_writably_mapped(mapping)"),
    }
    rejected = {}
    for name, (filename, token) in mutants.items():
        assert token in SOURCES[filename], (name, token)
        altered = dict(SOURCES)
        altered[filename] = altered[filename].replace(token, "")
        bad = [key for key, value in checks(altered).items() if not value]
        if not bad:
            raise SystemExit("G669 negative control escaped: " + name)
        rejected[name] = bad

    result = {
        "stage": "G669",
        "source_gates_pass": len(valid),
        "gates": valid,
        "mutated_source_rejections": rejected,
        "key_arch_correction": (
            "For ARMv7 update_mmu_cache_range is an intentional no-op. "
            "set_ptes performs __sync_icache_dcache before publishing valid user PTEs. "
            "An earlier G668 guard observation still need not exclude later writable "
            "mmap creation and write while no-ref RCU copy is in progress."
        ),
        "not_proven": [
            "No physical Cortex-A9 wrong-byte read, UAF, cache alias failure or security issue observed.",
            "Presence of PTE sync/flush is not a whole-core coherence or ordering proof.",
            "Actual physical D-cache VIPT aliasing vs nonaliasing runtime classification not read.",
            "Host G665 XArray userspace binary remains unrun without liburcu-dev.",
            "S0 PASS, S1 ACTIVE, S2 and S3 BLOCKED; no-ref RCU remains OFF."
        ],
    }
    OUT.write_text(json.dumps(result, indent=2) + "\n")
    print(f"G669: {len(valid)} ARMv7 fault/PTE source gates PASS, "
          f"{len(rejected)} mutated source controls REJECT; S2/S3 BLOCKED")


if __name__ == "__main__":
    main()
