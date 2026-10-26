#!/usr/bin/env python3
import hashlib
import json
import subprocess
from pathlib import Path

kernel = Path(__file__).resolve().parents[4]
project = kernel.parent
exp = project / "build/g686-reproducible-exp-on/vmlinux"
off = project / "build/g700-debugfs-rcu-off/vmlinux"
boot = project / "artifacts/G700-RCU-OFF-DEBUGFS-HOST-ONLY/BOOT-G700-RCU-OFF-DEBUGFS-HOST-ONLY-DO-NOT-FLASH.img"

def symbols(binary, with_size):
    command = ["arm-linux-gnueabihf-nm", "-g"]
    if with_size:
        command.append("-S")
    command.extend(["--defined-only", str(binary)])
    result = {}
    for line in subprocess.run(command, capture_output=True, text=True, check=True).stdout.splitlines():
        parts = line.split()
        if len(parts) == (4 if with_size else 3):
            result[parts[-1]] = {"type": parts[-2], "bytes": int(parts[1], 16)} if with_size else parts[-2]
    return result

hashes = {name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in
          (("experiment_vmlinux", exp), ("release_vmlinux", off), ("release_boot", boot))}
names_exp = symbols(exp, False)
names_off = symbols(off, False)
sizes_exp = symbols(exp, True)
sizes_off = symbols(off, True)
changed = [
    {"name": name, "type": sizes_exp[name]["type"],
     "experiment_bytes": sizes_exp[name]["bytes"],
     "release_bytes": sizes_off[name]["bytes"]}
    for name in sorted(sizes_exp.keys() & sizes_off.keys())
    if sizes_exp[name] != sizes_off[name]
]
expected = {
    "__alloc_frozen_pages_noprof", "alloc_contig_frozen_range_noprof",
    "filemap_read_rcu", "filemap_read", "filemap_read_accessed",
    "free_pages_prepare", "free_unref_folios", "rcu_free_sync",
    "setup_per_cpu_pageset", "mmap_region", "pageflag_names",
    "kallsyms_names", "kallsyms_offsets", "fuse_file_io_open",
    "fuse_ctl_add_conn", "fuse_conn_init", "fuse_copy_folio", "fuse_direct_io"
}
fuse_delta = [item for item in changed if item["name"].startswith("fuse_")]
fuse_source_diff = subprocess.run(
    ["git", "diff", "--name-only", "a2fca4558", "HEAD", "--", "fs/fuse"],
    cwd=kernel, capture_output=True, text=True, check=True
).stdout.strip()
checks = {
    "same_26742_global_defined_names": len(names_exp) == len(names_off) == 26742
        and names_exp.keys() == names_off.keys(),
    "all_global_symbol_types_identical": names_exp == names_off,
    "same_26547_sized_global_symbols": len(sizes_exp) == len(sizes_off) == 26547
        and sizes_exp.keys() == sizes_off.keys(),
    "exactly_18_known_size_deltas": {item["name"] for item in changed} == expected,
    "five_FUSE_functions_differ_in_linked_code_size": len(fuse_delta) == 5,
    "fs_fuse_source_unchanged_since_G650": not fuse_source_diff,
    "G700_host_image_unchanged": hashes["release_boot"] ==
        "aec9655acc727ab01d0bde3711414f9606c58c5fdcafb6ae5c52a2d3346edc7f",
    "G700_host_kernel_unchanged": hashes["release_vmlinux"] ==
        "027fa9b9cce7faf3b67b35a2bc256f0f34f078b47b75be0e3e5d42caa353715f"
}
report = {
    "stage": "G702",
    "hardware": "Samsung SM-T311 Cortex-A9 ARMv7",
    "comparison": "G686 full ARM32 EXP vs G700 full ARM32 RCU OFF DEBUG_FS",
    "build_hashes": hashes,
    "global_symbols": len(names_exp),
    "sized_global_symbols": len(sizes_exp),
    "symbols_without_explicit_size": len(names_exp) - len(sizes_exp),
    "source_fuse_diff": fuse_source_diff,
    "changed_symbol_sizes": changed,
    "fuse_changed_sizes": fuse_delta,
    "checks": checks,
    "static_name_compatibility_only": True,
    "full_module_ABI_or_runtime_compatibility_proven": False,
    "physical_fuse_tested_on_G700": False,
    "physical_p9_p10_verified": False,
    "flash_authorized": False
}
print(json.dumps(report, indent=2, ensure_ascii=False))
if not all(checks.values()):
    raise SystemExit(1)
