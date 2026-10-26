#!/usr/bin/env python3
import hashlib
import json
import re
import subprocess
from pathlib import Path

kernel = Path(__file__).resolve().parents[4]
project = kernel.parent
builds = {
    "experiment": project / "build/g686-reproducible-exp-on",
    "release": project / "build/g700-debugfs-rcu-off",
}
names = (
    "fuse_file_io_open", "fuse_copy_folio", "fuse_conn_init",
    "fuse_ctl_add_conn", "fuse_direct_io",
)

def disassembly(binary, name):
    raw = subprocess.run(
        ["arm-linux-gnueabihf-objdump", "-d", "--disassemble=" + name, str(binary)],
        text=True, capture_output=True, check=True
    ).stdout
    rows = [
        match.groups() for line in raw.splitlines()
        if (match := re.match(r"^\s*[0-9a-f]+:\s+([0-9a-f]{8})\s+([.]?\w+)\s*(.*)$", line))
    ]
    if not rows:
        raise RuntimeError(name)
    return {
        "arm_instructions": sum(op != ".word" for _, op, _ in rows),
        "literal_pool_words": sum(op == ".word" for _, op, _ in rows),
        "calls": re.findall(r"\bblx?\s+[0-9a-f]+\s+<([^>]+)>", raw),
        "body": "\n".join(op + " " + args for _, op, args in rows),
    }

previous = json.loads((kernel / "Documentation/mm/sm-t311-rcu-linked-delta-g702.json").read_text())
code = {variant: {} for variant in builds}
files = {variant: {} for variant in builds}
for variant, build in builds.items():
    path = build / "vmlinux"
    assert hashlib.sha256(path.read_bytes()).hexdigest() == previous["build_hashes"][
        "experiment_vmlinux" if variant == "experiment" else "release_vmlinux"
    ]
    for name in names:
        code[variant][name] = disassembly(path, name)
    for source in ("file", "dev", "inode", "control", "iomode"):
        obj = build / ("fs/fuse/" + source + ".o")
        cmd = build / ("fs/fuse/." + source + ".o.cmd")
        files[variant][source] = {
            "object_sha256": hashlib.sha256(obj.read_bytes()).hexdigest(),
            "command_sha256": hashlib.sha256(cmd.read_bytes()).hexdigest(),
        }

src = {n: (kernel / ("fs/fuse/" + n)).read_text() for n in (
    "control.c", "inode.c", "iomode.c", "dev.c", "fuse_i.h"
)}
header = (kernel / "include/linux/page-flags.h").read_text()
cfg = {k: (v / ".config").read_text() for k, v in builds.items()}
e = code["experiment"]
r = code["release"]
table = {item["name"]: item for item in previous["fuse_changed_sizes"]}
counts = {
    variant: {
        name: {
            "arm_instructions": row["arm_instructions"],
            "literal_pool_words": row["literal_pool_words"],
            "linked_bytes": table[name]["experiment_bytes" if variant == "experiment" else "release_bytes"],
            "call_count": len(row["calls"]),
        }
        for name, row in methods.items()
    }
    for variant, methods in code.items()
}
checks = {
    "pinned_existing_ARM32_ELF_hashes": True,
    "exact_five_FUSE_sized_symbols": set(table) == set(names),
    "all_A32_instruction_lengths_match_nm": all(
        (counts[v][n]["arm_instructions"] + counts[v][n]["literal_pool_words"]) * 4 == counts[v][n]["linked_bytes"]
        for v in builds for n in names
    ),
    "same_FUSE_compile_commands_but_different_objects": all(
        files["experiment"][n]["command_sha256"] == files["release"][n]["command_sha256"]
        and files["experiment"][n]["object_sha256"] != files["release"][n]["object_sha256"]
        for n in files["experiment"]
    ),
    "experiment_two_Kconfig_flags_ON_release_OFF": (
        "CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y" in cfg["experiment"]
        and "# CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT is not set" in cfg["release"]
        and "CONFIG_PAGE_RCU_FREE=y" in cfg["experiment"]
        and "CONFIG_PAGE_RCU_FREE=y" not in cfg["release"]
    ),
    "DEBUG_FS_FUSE_kept_built_in": all(
        "CONFIG_DEBUG_FS=y" in c and "CONFIG_FUSE_FS=y" in c for c in cfg.values()
    ),
    "fuse_conn_has_conditional_atomic64_field": (
        "#ifdef CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT" in src["fuse_i.h"]
        and "atomic64_t passthrough_successful_opens;" in src["fuse_i.h"]
    ),
    "fuse_conn_init_memset_240_to_224": (
        "mov r2, #240" in e["fuse_conn_init"]["body"]
        and "mov r2, #224" in r["fuse_conn_init"]["body"]
        and "atomic64_set(&fc->passthrough_successful_opens, 0);" in src["inode.c"]
    ),
    "experiment_atomic_counter_in_passthrough_open_only": (
        "atomic64_inc(&fc->passthrough_successful_opens);" in src["iomode.c"]
        and "ldrexd " in e["fuse_file_io_open"]["body"]
        and "strexd " in e["fuse_file_io_open"]["body"]
        and "ldrexd " not in r["fuse_file_io_open"]["body"]
        and "strexd " not in r["fuse_file_io_open"]["body"]
    ),
    "experiment_extra_passthrough_opens_control_entry": (
        'fuse_ctl_add_dentry(parent, fc, "passthrough_opens"' in src["control.c"]
        and e["fuse_ctl_add_conn"]["calls"].count("fuse_ctl_add_dentry") == 7
        and r["fuse_ctl_add_conn"]["calls"].count("fuse_ctl_add_dentry") == 6
    ),
    "FUSE_folio_RCU_tag_check_conditional_by_PAGE_RCU_FREE": (
        "if (folio_test_rcu_free(newfolio))" in src["dev.c"]
        and "#ifdef CONFIG_PAGE_RCU_FREE" in header
        and "FOLIO_TEST_FLAG_FALSE(rcu_free)" in header
        and "tst r3, #1048576" in e["fuse_copy_folio"]["body"]
        and "tst r3, #1048576" not in r["fuse_copy_folio"]["body"]
    ),
    "FUSE_direct_io_member_load_offset_shift": (
        "ldr r6, [r6, #128]" in e["fuse_direct_io"]["body"]
        and "ldr r6, [r6, #112]" in r["fuse_direct_io"]["body"]
    ),
    "source_fs_fuse_identical_since_G650": not subprocess.run(
        ["git", "diff", "--name-only", "a2fca4558", "HEAD", "--", "fs/fuse"],
        cwd=kernel, text=True, capture_output=True, check=True
    ).stdout.strip(),
    "not_runtime_ABI_or_perf_proof": previous["full_module_ABI_or_runtime_compatibility_proven"] is False,
    "no_flash_permission": previous["flash_authorized"] is False,
}
report = {
    "stage": "G703",
    "profile": "ARM EABI5, two existing full host vmlinux builds, no compilation",
    "five_FUSE_function_lengths": counts,
    "five_FUSE_object_compilation_fingerprints": files,
    "checks": checks,
    "conclusion": "Different FUSE binary is explained by conditional diagnostics, struct fuse_conn layout and PAGE_RCU_FREE page-flag logic; semantics and ABI are not proven equivalent",
    "fuse_conn_initializer_size_EXP": 240,
    "fuse_conn_initializer_size_OFF": 224,
    "passthrough_opens_diagnostic_in_G700": False,
    "RCU_free_flag_in_G700": False,
    "physical_G700_FUSE_tested": False,
    "hardware_qualification": "BLOCKED",
    "flash_authorized": False,
}
print(json.dumps(report, indent=2, ensure_ascii=False))
if not all(checks.values()):
    raise SystemExit(1)
