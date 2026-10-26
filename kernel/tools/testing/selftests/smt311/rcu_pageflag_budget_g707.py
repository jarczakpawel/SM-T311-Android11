#!/usr/bin/env python3
"""G707 SM-T311: independently calculate and ARM-cross-compile real page flag layout."""
import hashlib
import json
import re
import subprocess
from pathlib import Path

kernel = Path(__file__).resolve().parents[4]
project = kernel.parent
names = {
    "G686_EXP": "g686-reproducible-exp-on",
    "G700_OFF": "g700-debugfs-rcu-off",
}
previous = json.loads(
    (kernel / "Documentation/mm/sm-t311-rcu-pageflags-layout-g706.json").read_text()
)
sizes = {k: project / "build" / v for k, v in names.items()}
layout_source = (kernel / "include/linux/page-flags-layout.h").read_text()
mmzone_source = (kernel / "include/linux/mmzone.h").read_text()
alloc_source = (kernel / "mm/alloc_tag.c").read_text()
alloc_header = (kernel / "include/linux/pgalloc_tag.h").read_text()
kconfig = (kernel / "mm/Kconfig").read_text()
builthdr = {}
profiles = {}
configs = {}

def header_int(text, key):
    matches = re.findall(r"(?m)^#define\s+" + re.escape(key) + r"\s+(\d+)\b", text)
    if len(matches) != 1:
        raise RuntimeError(f"Expected exactly one generated {key}")
    return int(matches[0])

def calculate(bounds, config):
    flags = header_int(bounds, "NR_PAGEFLAGS")
    maxzones = header_int(bounds, "MAX_NR_ZONES")
    gens = header_int(bounds, "LRU_GEN_WIDTH")
    max_refs = header_int(bounds, "__LRU_REFS_WIDTH")
    cpus_bits = header_int(bounds, "NR_CPUS_BITS")
    assert "# CONFIG_SPARSEMEM_MANUAL is not set" in config
    assert "CONFIG_FLATMEM=y" in config and "CONFIG_HIGHMEM=y" in config
    assert "CONFIG_LRU_GEN=y" in config and "CONFIG_LRU_GEN_ENABLED=y" in config
    assert "# CONFIG_MEM_ALLOC_PROFILING is not set" in config
    assert "CONFIG_NUMA=" not in config and "CONFIG_NUMA_BALANCING=" not in config
    assert "CONFIG_KASAN=" not in config
    assert "CONFIG_NODES_SHIFT=" not in config
    # Actual page-flags-layout.h conditions: FLATMEM/no NUMA/KASAN.
    zones = 0 if maxzones < 2 else 1 if maxzones <= 2 else 2 if maxzones <= 4 else 3
    non_variable = zones + gens
    refs = min(max_refs, 32 - flags - non_variable)
    if refs < 0:
        raise RuntimeError("Negative LRU reference bit budget")
    unused = 32 - (flags + non_variable + refs)
    return {
        "BITS_PER_LONG": 32,
        "NR_PAGEFLAGS": flags,
        "MAX_NR_ZONES": maxzones,
        "NR_CPUS_BITS": cpus_bits,
        "ZONES_WIDTH": zones,
        "NODES_WIDTH": 0,
        "SECTIONS_WIDTH": 0,
        "LAST_CPUPID_WIDTH": 0,
        "KASAN_TAG_WIDTH": 0,
        "LRU_GEN_WIDTH": gens,
        "__LRU_REFS_WIDTH": max_refs,
        "LRU_REFS_WIDTH": refs,
        "NR_NON_PAGEFLAG_BITS": non_variable + refs,
        "NR_UNUSED_PAGEFLAG_BITS": unused,
        "ZONES_PGOFF": 32 - zones,
        "LRU_GEN_PGOFF": 32 - zones - gens,
        "LRU_REFS_PGOFF": 32 - zones - gens - refs,
        "theoretical_max_alloc_tag_bits_capacity": 1 << unused,
        "MEM_ALLOC_PROFILING_enabled": False,
    }

def cross_compile(bounds, config, expect, wrong=None):
    # Compile ORIGINAL unmodified layout macro body with generated bounds
    # and the exact y feature set. Only replace header dependencies with
    # the corresponding generated bounds and known target NUMA constant.
    src = layout_source.replace("#include <linux/numa.h>", "").replace(
        "#include <generated/bounds.h>", ""
    )
    if src == layout_source:
        raise RuntimeError("Real page flags layout header dependencies changed")
    defines = "".join(
        "#define " + line.split("=", 1)[0] + " 1\n"
        for line in config.splitlines()
        if re.fullmatch(r"CONFIG_[A-Z0-9_]+=y", line)
    )
    prelude = (
        "#define BITS_PER_LONG (__SIZEOF_LONG__*8)\n"
        "#define NODES_SHIFT 0\n"
        "#define min(a,b) ((a)<(b)?(a):(b))\n"
        + defines + bounds + src
    )
    expected = dict(expect)
    if wrong is not None:
        expected[wrong[0]] = wrong[1]
    assertions = (
        '_Static_assert(sizeof(unsigned long) == 4, "ARM EABI5 word");\n'
        + "".join(
            f'_Static_assert({name} == {value}, "{name}");\n'
            for name, value in expected.items()
            if name in (
                "NR_PAGEFLAGS", "ZONES_WIDTH", "NODES_WIDTH",
                "SECTIONS_WIDTH", "LAST_CPUPID_WIDTH", "KASAN_TAG_WIDTH",
                "LRU_GEN_WIDTH", "LRU_REFS_WIDTH", "NR_NON_PAGEFLAG_BITS",
                "NR_UNUSED_PAGEFLAG_BITS",
            )
        )
    )
    result = subprocess.run(
        ["arm-linux-gnueabihf-gcc", "-std=gnu11", "-x", "c",
         "-fsyntax-only", "-Werror", "-"],
        input=prelude + assertions, capture_output=True, text=True, check=False
    )
    return result.returncode == 0

for name, folder in sizes.items():
    config = (folder / ".config").read_text()
    bounds = (folder / "include/generated/bounds.h").read_text()
    builthdr[name] = bounds
    configs[name] = config
    profiles[name] = calculate(bounds, config)

a = profiles["G686_EXP"]
b = profiles["G700_OFF"]
checks = {
    "G706_previous_source_enum_compiled_controls_passed": all(previous["checks"].values()) and all(previous["negative_controls"].values()),
    "ARM32_generated_bits_NR_PAGEFLAGS_match_G706": (
        a["NR_PAGEFLAGS"] == previous["pageflag_values"]["G686_EXP"]["__NR_PAGEFLAGS"]
        and b["NR_PAGEFLAGS"] == previous["pageflag_values"]["G700_OFF"]["__NR_PAGEFLAGS"]
    ),
    "both_generated_MAX_NR_ZONES_three": a["MAX_NR_ZONES"] == b["MAX_NR_ZONES"] == 3,
    "both_generated_NR_CPUS_BITS_one": a["NR_CPUS_BITS"] == b["NR_CPUS_BITS"] == 1,
    "both_generated_LRU_GEN_WIDTH_three": a["LRU_GEN_WIDTH"] == b["LRU_GEN_WIDTH"] == 3,
    "both_generated_max_LRU_REFS_WIDTH_two": a["__LRU_REFS_WIDTH"] == b["__LRU_REFS_WIDTH"] == 2,
    "both_layouts_satisfy_32bit_word_budget": all(
        x["NR_PAGEFLAGS"] + x["NR_NON_PAGEFLAG_BITS"] + x["NR_UNUSED_PAGEFLAG_BITS"] == 32
        for x in profiles.values()
    ),
    "two_zone_bits_both_profiles": a["ZONES_WIDTH"] == b["ZONES_WIDTH"] == 2,
    "three_MGLRU_generation_bits_both_profiles": a["LRU_GEN_WIDTH"] == b["LRU_GEN_WIDTH"] == 3,
    "two_MGLRU_reference_bits_both_profiles": a["LRU_REFS_WIDTH"] == b["LRU_REFS_WIDTH"] == 2,
    "no_nodes_sections_cpupid_kasan_bits_both": all(
        x[k] == 0
        for x in profiles.values()
        for k in ("NODES_WIDTH", "SECTIONS_WIDTH", "LAST_CPUPID_WIDTH", "KASAN_TAG_WIDTH")
    ),
    "G686_three_G700_four_unused_bits": a["NR_UNUSED_PAGEFLAG_BITS"] == 3 and b["NR_UNUSED_PAGEFLAG_BITS"] == 4,
    "G700_unused_bit_is_not_extra_MGLRU_tier": a["LRU_REFS_WIDTH"] == b["LRU_REFS_WIDTH"],
    "zone_and_MGLRU_bit_offsets_unmodified": all(
        a[k] == b[k] for k in ("ZONES_PGOFF", "LRU_GEN_PGOFF", "LRU_REFS_PGOFF")
    ),
    "same_derived_non_pageflag_width_seven": a["NR_NON_PAGEFLAG_BITS"] == b["NR_NON_PAGEFLAG_BITS"] == 7,
    "cross_compiler_real_G686_header_static_asserts": cross_compile(builthdr["G686_EXP"], configs["G686_EXP"], a),
    "cross_compiler_real_G700_header_static_asserts": cross_compile(builthdr["G700_OFF"], configs["G700_OFF"], b),
    "alloc_profiling_Kconfig_disabled_and_guarded_in_both": (
        not any(x["MEM_ALLOC_PROFILING_enabled"] for x in profiles.values())
        and "#ifdef CONFIG_MEM_ALLOC_PROFILING" in alloc_header
    ),
    "alloc_tag_compression_capacity_based_on_unused_bits": (
        "kernel_tags.count > (1UL << NR_UNUSED_PAGEFLAG_BITS)" in alloc_source
        and "alloc_tag_ref_mask = ((1UL << NR_UNUSED_PAGEFLAG_BITS) - 1)" in alloc_source
    ),
    "source_LRU_REFS_width_bounded_not_expanding": (
        "LRU_REFS_WIDTH\tmin(__LRU_REFS_WIDTH," in layout_source
        and "NR_NON_PAGEFLAG_BITS\t(SECTIONS_WIDTH + NODES_WIDTH + ZONES_WIDTH +" in layout_source
    ),
    "source_offsets_derived_from_32bit_metadata_budget": (
        "#define ZONES_PGOFF" in mmzone_source
        and "#define LRU_GEN_PGOFF" in mmzone_source
        and "#define LRU_REFS_PGOFF" in mmzone_source
    ),
    "G700_unmodified_host_BOOT_SHA": hashlib.sha256(
        (project / "artifacts/G700-RCU-OFF-DEBUGFS-HOST-ONLY/BOOT-G700-RCU-OFF-DEBUGFS-HOST-ONLY-DO-NOT-FLASH.img").read_bytes()
    ).hexdigest() == "aec9655acc727ab01d0bde3711414f9606c58c5fdcafb6ae5c52a2d3346edc7f",
}
controls = {
    "reject_G686_wrong_unused_four": not cross_compile(builthdr["G686_EXP"], configs["G686_EXP"], a, ("NR_UNUSED_PAGEFLAG_BITS", 4)),
    "reject_G700_wrong_unused_three": not cross_compile(builthdr["G700_OFF"], configs["G700_OFF"], b, ("NR_UNUSED_PAGEFLAG_BITS", 3)),
    "reject_G686_wrong_tier_width_three": not cross_compile(builthdr["G686_EXP"], configs["G686_EXP"], a, ("LRU_REFS_WIDTH", 3)),
    "reject_G700_wrong_zone_width_one": not cross_compile(builthdr["G700_OFF"], configs["G700_OFF"], b, ("ZONES_WIDTH", 1)),
}
report = {
    "stage": "G707",
    "source": "Original page-flags-layout.h, generated bounds.h, .config of actual ARMv7 G686/G700 builds; arm-linux-gnueabihf-gcc static_assert of unchanged layout formulas",
    "profiles": profiles,
    "checks": checks,
    "negative_controls": controls,
    "meaning": "G700 gains 1 unoccupied page->flags bit; zone and MGLRU metadata widths and bit positions remain unchanged; allocation profiling disabled in both configs",
    "alloc_tag_theoretical_capacity_only_if_enabled": {"G686_EXP": 8, "G700_OFF": 16},
    "actual_alloc_tag_profiling_active": False,
    "physical_G700_verified": False,
    "flash_authorized": False,
}
print(json.dumps(report, indent=2, ensure_ascii=False))
if not all(checks.values()) or not all(controls.values()):
    raise SystemExit(1)
