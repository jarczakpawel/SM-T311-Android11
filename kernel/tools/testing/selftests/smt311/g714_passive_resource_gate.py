#!/usr/bin/env python3
"""G714 SM-T311 passive G650 CPU / ZRAM / memory-pressure release decision.

Reads a SANITIZED, READ-ONLY shell snapshot from the device, linked
existing G700 host .config and Exynos4212 DT OPP range. Source-aware
negative controls guard against silently calling a pressured system healthy.
NEVER tunes governors, ZRAM, OPP, swap, thermal or physical device.
"""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

KERNEL = Path(__file__).resolve().parents[4]
DEFAULT = KERNEL / "Documentation/mm/sm-t311-g714-physical-readonly-sample.txt"
CONFIG = (KERNEL.parent / "build/g700-debugfs-rcu-off/.config").read_text()
DTS = (KERNEL / "arch/arm/boot/dts/samsung/exynos4212.dtsi").read_text()

def extract(raw: str, begin: str, end: str) -> str:
    if raw.count(begin) != 1 or raw.count(end) != 1:
        raise ValueError("Missing or ambiguous snapshot marker " + begin)
    a = raw.index(begin) + len(begin)
    b = raw.index(end, a)
    return raw[a:b].strip()

def parse(data: str):
    if "G714_RESOURCE_BEGIN" not in data or "G714_RESOURCE_END" not in data:
        raise ValueError("Not a complete G714 device capture")
    scope=extract(data, "G714_RESOURCE_BEGIN", "G714_RESOURCE_END")
    k={}
    for line in scope.splitlines():
        if "=" in line:
            left,right=line.split("=",1)
            k[left.strip()] = right.strip()
    def section(name):
        b=extract(scope,name+"_BEGIN",name+"_END")
        parsed={}
        for line in b.splitlines():
            line=line.strip()
            if ":" in line:
                key,val=line.split(":",1)
            else:
                parts=line.split()
                if len(parts)<2: continue
                key,val=parts[0],parts[1]
            numeric=re.search(r"^-?\d+",val.strip())
            if numeric:
                parsed[key.strip()]=int(numeric.group())
            elif val.strip():
                parsed[key.strip()]=val.strip()
        return parsed
    t1=extract(scope, "CPU_TIS_START\n", "CPU_TIS_START_END")
    t2=extract(scope, "CPU_TIS_END\n", "CPU_TIS_END_END")
    def ticks(raw):
        result={}
        for line in raw.splitlines():
            match=re.fullmatch(r"(\d+)\s+(\d+)",line.strip())
            if not match:
                raise ValueError("Unparseable cpufreq residency line: " + line)
            freq,tick=map(int,match.groups())
            if freq in result: raise ValueError("Duplicate frequency")
            result[freq]=tick
        if not result: raise ValueError("Empty CPU time_in_state")
        return result
    return {
        "key_values":k,
        "memory":section("MEMORY"),
        "vmstat":section("VMSTAT"),
        "battery":section("BATTERY"),
        "ticks_start":ticks(t1),
        "ticks_end":ticks(t2),
    }

def inspect(raw: str, config: str = CONFIG, dts: str = DTS):
    obj=parse(raw)
    k,m,v,b=obj["key_values"],obj["memory"],obj["vmstat"],obj["battery"]
    first,last=obj["ticks_start"],obj["ticks_end"]
    freqs=set(first)&set(last)
    deltas={f:last[f]-first[f] for f in sorted(freqs)}
    total=sum(deltas.values())
    try: elapsed=float(k["END_UPTIME"])-float(k["START_UPTIME"])
    except (ValueError,KeyError): elapsed=-1
    good_time=elapsed>0 and 50*elapsed<=total<=200*elapsed
    shares={str(f):round(100*x/total,2) if total>0 else 0
            for f,x in deltas.items()}
    missing=object()
    tests={
        "capture_from_running_G650_kernel":k.get("KERNEL")=="7.3.0-pjarczak",
        "Android_boot_completed":k.get("BOOT_COMPLETED")=="1",
        "unprivileged_shell_no_escalation":k.get("ADB_UID")=="2000",
        "both_A9_cores_online":k.get("CPU_ONLINE")=="0-1",
        "cpufreq_schedutil_physically_selected":k.get("CPU_scaling_governor")=="schedutil",
        "CPU_min_200MHz":k.get("CPU_scaling_min_freq")=="200000",
        "CPU_max_1400MHz_no_1500_turbo":k.get("CPU_cpuinfo_max_freq")=="1400000" and last.get(1500000)==0 and first.get(1500000)==0,
        "reported_instantaneous_cpu_freq_in_opp_range":k.get("CPU_scaling_cur_freq","").isdigit() and 200000<=int(k.get("CPU_scaling_cur_freq","0"))<=1400000,
        "cpu_residency_two_snapshots_share_same_frequencies":set(first)==set(last),
        "cpu_time_in_state_monotonic":bool(deltas) and all(x>=0 for x in deltas.values()),
        "cpu_clock_residency_consistent_with_elapsed_time":good_time,
        "cpu_frequency_scales_down_and_up":deltas.get(200000,0)>0 and deltas.get(1400000,0)>0,
        "config_matches_schedutil_zram_swap_mglru":all(
            f"CONFIG_{name}=y" in config for name in
            ("CPU_FREQ", "CPU_FREQ_GOV_SCHEDUTIL", "ZRAM", "SWAP", "LRU_GEN")
        ),
        "config_default_compressor_is_lz4_not_physical_proof":'CONFIG_ZRAM_DEF_COMP="lz4"' in config,
        "cpu_1400MHz_valid_Exynos4212_DT_OPP":'opp-hz = /bits/ 64 <1400000000>;' in dts,
        "cpu_1500MHz_marked_turbo_not_unlocked": "opp-hz = /bits/ 64 <1500000000>;" in dts and "turbo-mode;" in dts,
        "physical_zram_active_via_swap_size":m.get("SwapTotal",0)==786428 and m.get("SwapTotal")==m.get("SwapFree"),
        "zram_sysfs_unreadable_not_treated_as_failure":all(k.get("ZRAM_"+n)=="UNAVAILABLE" for n in ("disksize","comp_algorithm","mm_stat")),
        "plenty_of_available_memory_at_sample":m.get("MemAvailable",0)>512*1024,
        "HighMem_present_and_lowmem_distinct":m.get("HighTotal",0)>0 and m.get("LowTotal",0)>0 and m.get("HighFree",-1)>=0,
        "no_observed_swap_io_since_boot":v.get("pswpin")==0 and v.get("pswpout")==0,
        "no_observed_OOM_or_direct_reclaim_since_boot":v.get("oom_kill")==0 and v.get("pgscan_direct")==0 and v.get("pgsteal_direct")==0,
        "no_observed_allocator_stall_or_compaction_stall":all(
            v.get(x)==0 for x in
            ("allocstall_normal","allocstall_high","allocstall_movable","compact_stall")
        ),
        "battery_present_full_USB_charge":b.get("present")=="true" and b.get("USB powered")=="true" and b.get("level")==100,
        "battery_temperature_plausible_not_SoC_sensor":isinstance(b.get("temperature"),int) and -100 < b["temperature"] < 800,
    }
    result={
        "stage":"G714",
        "environment":"physical read-only G650 resource sample, one short 4-second time_in_state interval, NO performance benchmark",
        "tests":tests,
        "observations":{
            "uptime_start_seconds":float(k.get("START_UPTIME",0)),
            "uptime_end_seconds":float(k.get("END_UPTIME",0)),
            "sampling_elapsed_seconds":round(elapsed,2),
            "CPU_200MHz_residency_percent":shares.get("200000"),
            "CPU_1400MHz_residency_percent":shares.get("1400000"),
            "CPU_other_residency_percent":round(100-shares.get("200000",0)-shares.get("1400000",0),2),
            "CPU_1500MHz_turbo_time_delta":deltas.get(1500000),
            "MemAvailable_KiB":m.get("MemAvailable"),
            "HighFree_KiB":m.get("HighFree"),
            "LowFree_KiB":m.get("LowFree"),
            "SwapTotal_KiB":m.get("SwapTotal"),
            "SwapFree_KiB":m.get("SwapFree"),
            "page_swaps_out_since_boot":v.get("pswpout"),
            "OOM_kills_since_boot":v.get("oom_kill"),
            "direct_page_scans_since_boot":v.get("pgscan_direct"),
            "battery_temperature_C":b.get("temperature",0)/10,
            "battery_USB_powered":b.get("USB powered"),
            "cpu_governor":k.get("CPU_scaling_governor"),
            "ZRAM_sysfs_compression":k.get("ZRAM_comp_algorithm"),
        },
        "decision":"HOLD_ZRAM_RESIZE_CPU_GOVERNOR_OPP_TUNING_PENDING_MEASURED_BOTTLENECK",
        "limitations":[
            "/proc/stat returned EINVAL under Android shell: this snapshot cannot claim CPU busy/idle percentages.",
            "time_in_state percentages are residency over 4 seconds, NOT CPU utilization.",
            "Physical compression algorithm/sysfs zRAM mm_stat inaccessible to uid2000: CONFIG_ZRAM_DEF_COMP=lz4 describes G700 host ONLY.",
            "BatteryService temperature is battery sensor; not Exynos die temperature.",
            "Battery at 100% on USB: not an unplugged standby or battery-drain measurement.",
            "No evidence of CPU thermal throttle or frequency lock in this limited capture.",
            "No measured latency or savings from any kernel tuning; no need to change working ZRAM."
        ],
        "physical_kernel_modified":False,
        "G700_flashed":False
    }
    return result

def verify_mutants(raw: str):
    # Each fixture deliberately represents an ALERT condition or a corrupt
    # telemetry stream. None is a proposed physical write.
    altered={
        "swap_exhaustion":raw.replace("SwapFree:         786428","SwapFree:              0"),
        "unexpected_swap_out":raw.replace("pswpout 0","pswpout 5"),
        "physical_kernel_OOM":raw.replace("oom_kill 0","oom_kill 1"),
        "physical_direct_reclaim":raw.replace("pgscan_direct 0","pgscan_direct 17"),
        "lost_memory_available":raw.replace("MemAvailable:     893736","MemAvailable:     100000"),
        "CPU_residency_counter_backwards":raw.replace("1400000 1773649","1400000 1773000"),
        "CPU_overclock_1500_turbo":raw.replace("1500000 0\nCPU_TIS_END_END","1500000 1\nCPU_TIS_END_END"),
        "boot_not_completed":raw.replace("BOOT_COMPLETED=1","BOOT_COMPLETED=0"),
        "malformed_frequency_sample":raw.replace("200000 2618832", "frequency_bad"),
    }
    result={}
    original=inspect(raw)
    if not all(original["tests"].values()):
        raise ValueError("Original snapshot failed positive guards; cannot run mutation gates")
    for label,mutant in altered.items():
        if mutant == raw:
            raise RuntimeError("Mutation did not change raw input: " + label)
        try:
            attempt=inspect(mutant)
            result[label]=not all(attempt["tests"].values())
        except (ValueError, KeyError):
            result[label]=True
    return result

def main():
    a=argparse.ArgumentParser()
    a.add_argument("snapshot", nargs="?", type=Path, default=DEFAULT)
    args=a.parse_args()
    raw=args.snapshot.read_text()
    result=inspect(raw)
    result["negative_controls"]=verify_mutants(raw)
    print(json.dumps(result,indent=2,ensure_ascii=False))
    if not all(result["tests"].values()) or not all(result["negative_controls"].values()):
        raise SystemExit(1)

if __name__=="__main__":
    main()
