#!/usr/bin/env python3
import json
from pathlib import Path

root = Path(__file__).resolve().parents[4]
filemap = (root / "mm/filemap.c").read_text()
lookup = filemap.split("static struct folio *filemap_read_rcu_lookup(", 1)[1].split("static size_t filemap_read_rcu_size(", 1)[0]
eligible = filemap.split("static bool filemap_read_rcu_can_copy(", 1)[1].split("static struct folio *filemap_read_rcu_lookup(", 1)[0]
batch = filemap.split("static bool filemap_read_rcu_batch(", 1)[1].split("size_t filemap_read_rcu(", 1)[0]
copy = filemap.split("static size_t filemap_read_rcu_copy(", 1)[1].split("#define FILEMAP_RCU_READ_MAX", 1)[0]
highmem = (root / "mm/highmem.c").read_text()
arm_cache = (root / "arch/arm/include/asm/cachetype.h").read_text()
arm_flush = (root / "arch/arm/include/asm/cacheflush.h").read_text()
arm_flush_impl = (root / "arch/arm/mm/flush.c").read_text()
config = (root / "arch/arm/configs/sm_t311_android11_defconfig").read_text()

checks = {
    "sm_t311_highmem_enabled": "CONFIG_HIGHMEM=y" in config,
    "sm_t311_arm_smp_enabled": "CONFIG_SMP=y" in config,
    "lookup_excludes_large_folios": "folio_test_large(folio)" in lookup,
    "lookup_excludes_highmem": "folio_zonenum(folio) != ZONE_NORMAL" in lookup,
    "lookup_checks_mapping_identity": "READ_ONCE(folio->mapping) != mapping" in lookup,
    "copy_eligibility_excludes_highmem": "folio_zonenum(folio) != ZONE_NORMAL" in eligible,
    "copy_eligibility_excludes_large_folios": "folio_test_large(folio)" in eligible,
    "arm_requires_dcache_flush": "#define ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE 1" in arm_flush,
    "writable_mmap_avoids_no_ref_copy": "ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE &&" in batch and "mapping_writably_mapped(mapping)" in batch,
    "copy_uses_kmap_local": "kmap_local_page(page)" in copy and "kunmap_local(kaddr)" in copy,
    "copy_within_rcu_and_pagefault_guard": "rcu_read_lock();" in batch and "pagefault_disable();" in batch and "pagefault_enable();" in batch and "rcu_read_unlock();" in batch,
    "normal_direct_map_highmem_temporary_map": "if (!IS_ENABLED(CONFIG_DEBUG_KMAP_LOCAL_FORCE_MAP) && !PageHighMem(page))" in highmem and "return page_address(page);" in highmem,
    "armv7_cache_type_dispatch_present": "#if __LINUX_ARM_ARCH__ >= 7" in arm_cache and "CACHEID_VIPT_NONALIASING" in arm_cache,
    "arm_cache_alias_flush_paths_present": "cache_is_vipt_aliasing()" in arm_flush_impl and "flush_pfn_alias(" in arm_flush_impl,
}
failed = [name for name, ok in checks.items() if not ok]
result = {
    "device": "Samsung SM-T311 ARMv7 Cortex-A9",
    "source_only": True,
    "production_boot": "G650",
    "checks": checks,
    "passed": len(checks) - len(failed),
    "total": len(checks),
    "remaining": [
        "These source guards do not prove that an unmarked recycled folio cannot alias an old speculative pointer.",
        "ARMv7 runtime cacheid and DMA behavior are not verified by this source-only check.",
        "No evidence here supports removing ZONE_NORMAL or writable-mmap exclusions.",
        "S2 and S3 remain BLOCKED, so physical RCU ON and ABBA are not authorized."
    ]
}
output = root / "Documentation/mm/sm-t311-highmem-source-g660.json"
output.write_text(json.dumps(result, indent=2) + "\n")
print(f"G660 source-only safety gate: {result['passed']}/{result['total']} conditions")
if failed:
    raise SystemExit("FAILED: " + ", ".join(failed))
