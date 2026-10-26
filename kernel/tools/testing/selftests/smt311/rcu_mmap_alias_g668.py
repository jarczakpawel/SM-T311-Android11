#!/usr/bin/env python3
"""G668 writable-mmap guard temporal window source audit and SC witness.

Not ARMv7 weak memory, a real cache-coherency failure, or proof of UAF.
An interleaving permitted even by sequential consistency merits separate
investigation before experimental no-ref RCU is enabled.
"""
import itertools
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
SOURCES = {
    name: (ROOT / name).read_text()
    for name in (
        "include/linux/fs.h", "mm/vma.c", "mm/filemap.c",
        "fs/ext4/inode.c", "arch/arm/mm/flush.c",
        "arch/arm/configs/sm_t311_android11_defconfig",
    )
}
OUT = ROOT / "Documentation/mm/sm-t311-rcu-mmap-alias-g668.json"

def func(text, name):
    for m in re.finditer(r"\b" + re.escape(name) + r"\s*\(", text):
        depth = 0
        pos = text.find("(", m.start())
        close = 0
        for i in range(pos, len(text)):
            depth += (text[i] == "(") - (text[i] == ")")
            if depth == 0:
                close = i + 1
                break
        if close and re.match(r"\s*\{", text[close:]):
            start = close + re.match(r"\s*\{", text[close:]).end()
            end = text.find("\n}", start)
            if end >= 0:
                return text[start:end]
    raise ValueError("Function body missing: " + name)

def ordered(text, *tokens):
    if not all(x in text for x in tokens):
        return False
    return all(text.index(tokens[i]) < text.index(tokens[i+1])
               for i in range(len(tokens)-1))

def guards(src):
    fs = src["include/linux/fs.h"]
    vma = src["mm/vma.c"]
    fm = src["mm/filemap.c"]
    arm = src["arch/arm/mm/flush.c"]
    defcfg = src["arch/arm/configs/sm_t311_android11_defconfig"]
    reader = func(fm, "filemap_read_rcu_batch")
    copy = func(fm, "filemap_read_rcu_copy")
    region = func(vma, "mmap_region")
    link = func(vma, "__vma_link_file")
    remove = func(vma, "__remove_shared_vm_struct")
    mark = func(src["fs/ext4/inode.c"], "ext4_page_mkwrite")
    flush = func(arm, "flush_dcache_folio")
    guard = func(fs, "mapping_writably_mapped")
    inc = func(fs, "mapping_map_writable")
    return {
        "arm_vipt_config": "CONFIG_CPU_CACHE_VIPT=y" in defcfg,
        "arm_smp_config": "CONFIG_SMP=y" in defcfg,
        "writable_counter_atomic_read": (
            "atomic_read(&mapping->i_mmap_writable) > 0" in guard),
        "writable_counter_atomic_increment": (
            "atomic_inc_unless_negative(&mapping->i_mmap_writable)" in inc),
        "file_mmap_preincrements_writable_counter": (
            ordered(region, "mapping_map_writable(file->f_mapping)",
                    "__mmap_region(file, addr, len, vma_flags, pgoff, uf)") ),
        "file_mmap_counter_drop_after_creation": ordered(
            region, "__mmap_region(file, addr, len, vma_flags, pgoff, uf)",
            "mapping_unmap_writable(file->f_mapping)"),
        "linked_shared_mapping_keeps_count": (
            "if (vma_is_shared_maywrite(vma))" in link and
            "mapping_allow_writable(mapping)" in link),
        "unlinked_shared_mapping_decrements_count": (
            "mapping_unmap_writable(mapping)" in remove),
        "reader_writable_guard_before_rcu": ordered(
            reader, "mapping_writably_mapped(mapping)",
            "rcu_read_lock();", "filemap_read_rcu_copy("),
        "reader_no_i_mmap_lock": (
            "i_mmap_lock_read(" not in reader and
            "i_mmap_lock_write(" not in reader),
        "reader_no_writer_counter_pin": (
            "mapping_deny_writable(" not in reader and
            "mapping_map_writable(" not in reader),
        "reader_rcu_unlocked_after_copy": ordered(
            reader, "rcu_read_lock();", "filemap_read_rcu_copy(",
            "rcu_read_unlock();"),
        "reader_direct_lowmem_copy": (
            "kmap_local_page(page)" in copy and
            "_copy_to_iter(kaddr + offset, len, iter)" in copy),
        "ext4_page_mkwrite_shared_invalidate": (
            "filemap_invalidate_lock_shared(mapping)" in mark),
        "arch_flush_has_alias_sensitive_path": (
            "cache_is_vipt_aliasing()" in arm and
            "__flush_dcache_folio(mapping, folio)" in flush),
        "arch_flush_has_mapping_based_lazy_branch": (
            "mapping && !folio_mapped(folio)" in flush and
            "clear_bit(PG_dcache_clean, &folio->flags.f)" in flush),
    }

R = ("R_check_zero", "R_lock", "R_copy", "R_unlock")
W = ("W_increment_count", "W_link_vma", "W_fault_page", "W_write_shared")

def schedules():
    for positions in itertools.combinations(range(8), 4):
        writer_positions = set(positions)
        ri, wi = iter(R), iter(W)
        yield tuple(next(wi) if i in writer_positions else next(ri)
                    for i in range(8))

def explore():
    cases = {
        "schedules": 0, "check_saw_zero": 0,
        "new_shared_write_before_copy": 0,
        "new_shared_vma_before_copy": 0,
    }
    examples = {}
    for steps in schedules():
        cases["schedules"] += 1
        count = 0
        check_was_zero = False
        is_linked = False
        did_write = False
        for e in steps:
            if e == "R_check_zero":
                check_was_zero = count == 0
                if check_was_zero:
                    cases["check_saw_zero"] += 1
            elif e == "W_increment_count":
                count += 1
            elif e == "W_link_vma":
                is_linked = True
            elif e == "W_write_shared":
                did_write = True
            elif e == "R_copy" and check_was_zero:
                if is_linked:
                    cases["new_shared_vma_before_copy"] += 1
                    examples.setdefault("vma", list(steps))
                if did_write:
                    cases["new_shared_write_before_copy"] += 1
                    examples.setdefault("write", list(steps))
    assert cases["schedules"] == 70
    assert cases["new_shared_write_before_copy"] > 0
    return cases, examples

def main():
    baseline = guards(SOURCES)
    assert len(baseline) == 16, len(baseline)
    bad = [k for k, ok in baseline.items() if not ok]
    if bad:
        raise SystemExit("G668 source check failed: " + ", ".join(bad))
    mutants = {
        "remove_mapping_atomic_counter_read": (
            "include/linux/fs.h", "atomic_read(&mapping->i_mmap_writable) > 0"),
        "remove_writer_counter_increment": (
            "include/linux/fs.h", "atomic_inc_unless_negative(&mapping->i_mmap_writable)"),
        "remove_reader_alias_guard": (
            "mm/filemap.c", "mapping_writably_mapped(mapping)"),
        "remove_persistent_mapping_counter": (
            "mm/vma.c", "mapping_allow_writable(mapping)"),
        "remove_cache_maintenance_call": (
            "arch/arm/mm/flush.c", "\t\t__flush_dcache_folio(mapping, folio);\n\t\tif (mapping && cache_is_vivt())"),
    }
    negative = {}
    for name, (filename, token) in mutants.items():
        assert token in SOURCES[filename], name
        changed = dict(SOURCES)
        changed[filename] = changed[filename].replace(token, "", 1)
        missing = [k for k, ok in guards(changed).items() if not ok]
        if not missing:
            raise SystemExit("G668 negative test escaped: " + name)
        negative[name] = missing

    counts, examples = explore()
    result = {
        "stage": "G668",
        "source_only": True,
        "source_anchors_pass": len(baseline),
        "source_anchors": baseline,
        "negative_mutants_rejected": negative,
        "bounded_sc_model": {
            "counts": counts,
            "witnesses": examples,
            "not_a_kernel_or_cache_coherency_test": True,
        },
        "finding": (
            "Atomic i_mmap_writable may transition 0->1 after the fast reader's "
            "one-time guard check, before/while the reader copies. The reader "
            "does not pin the counter or take i_mmap_rwsem. Source-program-order "
            "alone cannot rule out this interval; whether ARMv7 cache aliasing "
            "causes incorrect returned bytes is NOT demonstrated."
        ),
        "caveats": [
            "The counter counts VM_SHARED/VM_MAYWRITE rather than only currently writable PTEs.",
            "The writer counter is incremented before mmap creates a VMA; this narrows but does not close the race with an earlier reader check.",
            "RCU read lock concerns lifetime, not i_mmap_writable stabilization or alias flush semantics.",
            "The model is SC, not herd7, C atomic proof or physical dual Cortex-A9 test.",
            "S1 ACTIVE; S2/S3 BLOCKED; no-ref RCU stays OFF."
        ],
    }
    OUT.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n")
    print("G668:", len(baseline), "source checks PASS,",
          len(negative), "mutated source gates rejected;",
          counts["schedules"], "SC schedules,",
          counts["new_shared_write_before_copy"], "TOCTOU witnesses")


if __name__ == "__main__":
    main()
