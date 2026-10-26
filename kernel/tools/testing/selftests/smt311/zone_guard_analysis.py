#!/usr/bin/env python3
"""G654 Samsung SM-T311: read-only Normal/HighMem RCU reservation assessment.

Input is a SAVED /proc/zoneinfo snapshot from this tablet. No ADB, no
modification of sysctl/governor, no memory stress. Thresholds are read from
the actual kernel mm/internal.h and must match the reviewed page-shift
configuration. This is headroom measurement, NOT RCU-ON safety validation.
"""
import argparse
import json
import re
from pathlib import Path

KERNEL = Path(__file__).resolve().parents[4]
INTERNAL = KERNEL / "mm/internal.h"
EXPECTED = (
    ("SM_T311_RCU_PENDING_SOFT_LIMIT", "SZ_2M", 512),
    ("SM_T311_RCU_FREE_RESERVE", "SZ_4M", 1024),
)


def parse_zoneinfo(text):
    zones = {}
    zone = None
    for line in text.splitlines():
        m = re.match(r"^Node\s+(\d+), zone\s+([A-Za-z]+)$", line)
        if m:
            zone = m.group(2)
            zones[zone] = {"node": int(m.group(1))}
            continue
        if not zone:
            continue
        for attr, name in (
            (r"^\s+pages free\s+(\d+)$", "free_pages"),
            (r"^\s+min\s+(\d+)$", "min"),
            (r"^\s+low\s+(\d+)$", "low"),
            (r"^\s+high\s+(\d+)$", "high_watermark"),
            (r"^\s+managed\s+(\d+)$", "managed_pages"),
            (r"^\s+nr_free_rcu\s+(\d+)$", "pending_rcu_pages"),
        ):
            match = re.match(attr, line)
            if match and name not in zones[zone]:
                zones[zone][name] = int(match.group(1))
    for name in ("Normal", "HighMem"):
        assert name in zones, f"{name} absent"
        assert {"free_pages","min","low","high_watermark",
                "managed_pages","pending_rcu_pages"} <= zones[name].keys(), zones[name]
    return zones


def parse_source_limits():
    text = INTERNAL.read_text()
    params = {}
    for name, size, pages in EXPECTED:
        pattern = r"#define\s+"+name+r"\s+\("+size+r"\s*>>\s*PAGE_SHIFT\)"
        assert re.search(pattern, text), "unreviewed kernel RCU free limit: "+name
        params[name] = {"bytes":pages*4096, "pages":pages}
    return params


def analyze(raw):
    zones = parse_zoneinfo(raw)
    limits = parse_source_limits()
    n = zones["Normal"]
    threshold = n["high_watermark"] + limits["SM_T311_RCU_FREE_RESERVE"]["pages"]
    n["trigger_guard_below_or_equal_pages"] = threshold
    n["above_guard_margin_pages"] = n["free_pages"] - threshold
    n["above_guard_margin_MiB"] = round((n["free_pages"]-threshold)/256,3)
    n["would_resource_guard_pass_now"] = n["free_pages"] > threshold and n["pending_rcu_pages"] == 0
    h = zones["HighMem"]
    return {
        "device": "Samsung SM-T311 Exynos4212 ARMv7 Cortex-A9",
        "physically_installed_BOOT": "G650",
        "RCU_feature_tested_ON": False,
        "rcu_pending_soft_limit": limits["SM_T311_RCU_PENDING_SOFT_LIMIT"],
        "normal_extra_reserve": limits["SM_T311_RCU_FREE_RESERVE"],
        "zones": zones,
        "readiness_summary": {
            "normal_memory_guard_would_allow_this_snapshot": n["would_resource_guard_pass_now"],
            "highmem_below_high_watermark": h["free_pages"] < h["high_watermark"],
            "not_an_RCU_ON_safety_proof": True
        },
        "qualifications": [
            "Physical /proc/zoneinfo snapshot, not continuous RAM pressure measurement.",
            "Normal free far above threshold does not prove safe refcountless folio lifetime.",
            "HighMem free below high watermark can reflect reclaimable cache and does not automatically mean OOM.",
            "RCU feature needs exact machine match, page-flag and LKMM/HighMem safety gates beyond free memory.",
            "Do not remove Normal reserve limits or enable RCU ON on an unattended device."
        ]
    }


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("zoneinfo_file")
    ap.add_argument("--output")
    args=ap.parse_args()
    report = analyze(Path(args.zoneinfo_file).read_text())
    data = json.dumps(report, indent=2, ensure_ascii=False) + "\n"
    if args.output:
        Path(args.output).write_text(data)
    print("Normal free",report["zones"]["Normal"]["free_pages"],"pages; guard",
          report["zones"]["Normal"]["trigger_guard_below_or_equal_pages"],"pages; margin",
          report["zones"]["Normal"]["above_guard_margin_MiB"],"MiB; resource guard pass",
          report["zones"]["Normal"]["would_resource_guard_pass_now"])
    print("HighMem free",report["zones"]["HighMem"]["free_pages"],"pages; high watermark",
          report["zones"]["HighMem"]["high_watermark"],"pages")
    print("RCU ON SAFETY STILL NOT PROVEN")


if __name__ == "__main__":
    main()
