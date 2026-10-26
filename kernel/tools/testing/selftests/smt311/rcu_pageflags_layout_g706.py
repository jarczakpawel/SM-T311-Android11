#!/usr/bin/env python3
import hashlib
import json
import re
import subprocess
from pathlib import Path

kernel = Path(__file__).resolve().parents[4]
project = kernel.parent
builds = {
    "G686_EXP": project / "build/g686-reproducible-exp-on",
    "G700_OFF": project / "build/g700-debugfs-rcu-off",
}
configs = {name: (path / ".config").read_text() for name, path in builds.items()}
header = (kernel / "include/linux/page-flags.h").read_text()
uapi = (kernel / "include/uapi/linux/kernel-page-flags.h").read_text()
internal = (kernel / "include/linux/kernel-page-flags.h").read_text()
proc = (kernel / "fs/proc/page.c").read_text()
trace = (kernel / "mm/debug.c").read_text()
layout = (kernel / "include/linux/page-flags-layout.h").read_text()
folio = (kernel / "fs/fuse/dev.c").read_text()
previous = json.loads((kernel / "Documentation/mm/sm-t311-rcu-fuse-builtin-g705.json").read_text())

enum_start = header.index("enum pageflags {")
enum_stop = header.index("};", enum_start) + 2
enum_source = header[enum_start:enum_stop]

def derive_enum(config, content=enum_source):
    defines = "".join(
        "#define " + line.split("=", 1)[0] + " 1\n"
        for line in config.splitlines()
        if re.fullmatch(r"CONFIG_[A-Z0-9_]+=y", line)
    )
    source = subprocess.run(
        ["arm-linux-gnueabihf-gcc", "-E", "-P", "-x", "c", "-"],
        input=defines + content, text=True, capture_output=True, check=True
    ).stdout
    part = source.split("enum pageflags {", 1)[1].split("};", 1)[0]
    values = {}
    last = -1
    for token in part.split(","):
        token = token.strip()
        if not token:
            continue
        fields = token.split("=", 1)
        symbol = fields[0].strip()
        if len(fields) == 1:
            last += 1
        else:
            rhs = fields[1].strip()
            last = values[rhs] if rhs in values else int(rhs, 0)
        values[symbol] = last
    return values

flags = {name: derive_enum(config) for name, config in configs.items()}

def disassembly(build, symbol):
    return subprocess.run(
        ["arm-linux-gnueabihf-objdump", "-d", "--disassemble=" + symbol, str(build / "vmlinux")],
        text=True, capture_output=True, check=True
    ).stdout

def defined_sizes(build):
    entries = {}
    for line in subprocess.run(
        ["arm-linux-gnueabihf-nm", "-g", "-S", "--defined-only", str(build / "vmlinux")],
        text=True, capture_output=True, check=True
    ).stdout.splitlines():
        parts = line.split()
        if len(parts) == 4:
            entries[parts[3]] = (parts[2], int(parts[1], 16))
    return entries

sizes = {name: defined_sizes(path) for name, path in builds.items()}
fuse_asm = {name: disassembly(path, "fuse_copy_folio") for name, path in builds.items()}
release_boot = project / "artifacts/G700-RCU-OFF-DEBUGFS-HOST-ONLY/BOOT-G700-RCU-OFF-DEBUGFS-HOST-ONLY-DO-NOT-FLASH.img"
boot_hash = hashlib.sha256(release_boot.read_bytes()).hexdigest()

def checks_for(e, o, user_header=uapi, proc_source=proc, machine=fuse_asm):
    common = set(e) & set(o)
    stable_pre = [x for x in common if x.startswith("PG_") and x not in ("PG_readahead", "PG_swapcache")]
    return {
        "G705_previous_positive_and_mutation_gates": all(previous["checks"].values()) and all(previous["negative_controls"].values()),
        "preprocessed_real_ARM32_ON_enum_22": e.get("__NR_PAGEFLAGS") == 22,
        "preprocessed_real_ARM32_OFF_enum_21": o.get("__NR_PAGEFLAGS") == 21,
        "PG_rcu_free_only_in_exp_at_bit20": e.get("PG_rcu_free") == 20 and "PG_rcu_free" not in o,
        "PG_mlocked_shifts_21_to_20": e.get("PG_mlocked") == 21 and o.get("PG_mlocked") == 20,
        "preexisting_pageflags_below_rcu_free_stable": all(e[x] == o[x] for x in stable_pre if e[x] < 20),
        "no_memory_failure_or_arch_extra_flags_in_either": all(
            "PG_hwpoison" not in x and "PG_arch_2" not in x for x in (e, o)
        ),
        "user_UAPI_KPF_HWPOISON_stays_19": re.search(r"(?m)^#define\s+KPF_HWPOISON\s+19\b", user_header) is not None,
        "internal_KPF_MLOCKED_remains_33": re.search(r"(?m)^#define\s+KPF_MLOCKED\s+33\b", internal) is not None,
        "stable_page_flags_maps_public_bit_to_dynamic_kernel_PG": re.search(
            r"kpf_copy_bit\(k,\s*KPF_MLOCKED,\s*PG_mlocked\)", proc_source
        ) is not None,
        "no_public_KPF_RCU_FREE_number": "KPF_RCU_FREE" not in user_header and "KPF_RCU_FREE" not in internal,
        "static_pageflag_name_table_consistent_with_enum": (
            sizes["G686_EXP"].get("pageflag_names") == ("D", (22 + 1) * 8)
            and sizes["G700_OFF"].get("pageflag_names") == ("D", (21 + 1) * 8)
            and "BUILD_BUG_ON(ARRAY_SIZE(pageflag_names) != __NR_PAGEFLAGS + 1)" in trace
        ),
        "real_ARM32_stable_page_flags_function_size_unchanged": (
            sizes["G686_EXP"].get("stable_page_flags") == sizes["G700_OFF"].get("stable_page_flags") == ("T", 448)
        ),
        "FUSE_source_uses_proper_flag_accessor": "if (folio_test_rcu_free(newfolio))" in folio,
        "FUSE_real_ARM32_flag_mask_shift_observed": (
            "#2097152" in machine["G686_EXP"]
            and "#1048576" in machine["G700_OFF"]
        ),
        "pageflags_internal_unused_bits_derive_NR_PAGEFLAGS": (
            "NR_UNUSED_PAGEFLAG_BITS" in layout
            and "NR_NON_PAGEFLAG_BITS + NR_PAGEFLAGS" in layout
        ),
        "G700_RELEASE_BOOT_exact_pinned_digest": (
            boot_hash == "aec9655acc727ab01d0bde3711414f9606c58c5fdcafb6ae5c52a2d3346edc7f"
        ),
    }

checks = checks_for(flags["G686_EXP"], flags["G700_OFF"])
controls = {
    "remove_PG_rcu_free_from_experiment": not all(checks_for(
        derive_enum(configs["G700_OFF"]), flags["G700_OFF"]
    ).values()),
    "change_internal_MLOCKED_bit_mapping": not all(checks_for(
        flags["G686_EXP"], {**flags["G700_OFF"], "PG_mlocked": 21}
    ).values()),
    "remove_user_visible_mapping": not all(checks_for(
        flags["G686_EXP"], flags["G700_OFF"],
        proc_source=proc.replace("KPF_MLOCKED,\tPG_mlocked", "KPF_MLOCKED,\tPG_reclaim")
    ).values()),
    "remove_ARM32_mlocked_mask_evidence": not all(checks_for(
        flags["G686_EXP"], flags["G700_OFF"],
        machine={**fuse_asm, "G700_OFF": fuse_asm["G700_OFF"].replace("#1048576", "#2097152")}
    ).values()),
}
report = {
    "stage": "G706",
    "verification": "Real ARM32 host .config + target GCC preprocessing exact pageflag enum, linked vmlinux global size and disassembly",
    "pageflag_values": {
        label: {n: derived.get(n) for n in (
            "PG_locked", "PG_dropbehind", "PG_rcu_free", "PG_mlocked",
            "PG_hwpoison", "__NR_PAGEFLAGS"
        )}
        for label, derived in flags.items()
    },
    "pageflag_names_table_bytes": {
        label: sizes[label]["pageflag_names"][1] for label in builds
    },
    "checks": checks,
    "negative_controls": controls,
    "public_KPF_MLOCKED": 33,
    "public_KPF_HWPOISON": 19,
    "internal_pg_mlocked_shift_IS_ABI_break_for_external_module_using_raw_layout": "POSSIBLE; external modules not audited",
    "public_proc_kpageflags_numbering_compiled_mapping": "SOURCE CONFIRMED, NOT TESTED ON G700 DEVICE",
    "G700_kernel_boot_sha256": boot_hash,
    "physical_G700_booted": False,
    "runtime_ABI_proven": False,
    "flash_authorized": False,
}
print(json.dumps(report, ensure_ascii=False, indent=2))
if not all(checks.values()) or not all(controls.values()):
    raise SystemExit(1)
