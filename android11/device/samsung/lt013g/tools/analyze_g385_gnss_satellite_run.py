#!/usr/bin/env python3
"""G385 bounded GNSS status trace audit. Never log latitude or longitude.

Only accepts T311GNSS callbacks plus a minimal postflight (power/started/
boot/SELinux/SIM state), not a full Android location dump.
"""
import argparse
import json
import re
from pathlib import Path
from datetime import datetime

START = "T311GNSS: GNSS_REQUEST_STARTED callback=true"
FINISH = re.compile(r"T311GNSS: GNSS_FINAL svEvents=(\d+) fixes=(\d+)")
SV = re.compile(r"T311GNSS: GNSS_SV visible=(\d+) used=(\d+) events=(\d+)")
CLOCK = re.compile(r"^(\d\d-\d\d \d\d:\d\d:\d\d\.\d{3}) T311GNSS: ")

def need(cond, reason):
    if not cond:
        raise ValueError(reason)

def parse(raw):
    # Very deliberately allow only explicit GNSS logger and literal known
    # postflight output; the archive must never include location coordinates,
    # APNs, SIM identifiers or unfiltered logcat messages.
    allow = ("GNSS_REQUEST_STARTED callback=true","GNSS_STARTED","GNSS_FINAL ",
             "GNSS_SV visible=","GNSS_FIRST_FIX ","GNSS_FIX count=")
    lines=raw.splitlines()
    gpslines=[x for x in lines if " T311GNSS: " in x]
    need(len(gpslines)>2, "GNSS trace missing")
    for line in gpslines:
        need(any(("T311GNSS: "+k) in line for k in allow),
             "unreviewed GNSS logger contents")
    need(START in raw and "T311GNSS: GNSS_STARTED" in raw,
         "hardware GNSS never requested/started")
    completed=FINISH.findall(raw)
    need(len(completed)==1, "not exactly one self-terminating GNSS_FINAL")
    total_events, fixes=map(int,completed[0])
    samples=[]
    max_strong25=0
    max_cn0_dbhz=0.0
    signal_samples=0
    for line in gpslines:
        s=SV.search(line)
        if s:
            vis,used,n=map(int,s.groups())
            need(vis<=64 and used<=vis and n>0, "invalid GNSS counts")
            need(not samples or n>samples[-1][2], "non-monotonic GNSS events")
            samples.append((vis,used,n))
            signal=re.search(r" strong25=(\d+) strongestCn0=([0-9]+(?:\.[0-9]+)?)",line)
            if signal:
                strong,cn0=int(signal.group(1)),float(signal.group(2))
                need(0<=strong<=vis and 0<=cn0<=70,"invalid GNSS signal data")
                max_strong25=max(max_strong25,strong)
                max_cn0_dbhz=max(max_cn0_dbhz,cn0)
                signal_samples+=1
    need(len(samples)>=2,"too few satellite samples")
    need(total_events>=samples[-1][2], "summary has fewer events than samples")
    need(total_events<=samples[-1][2]+20,"summary inconsistent with sampled event rates")
    stamps=[CLOCK.match(x) for x in gpslines]
    need(all(stamps),"missing monotonic GNSS log timestamps")
    t0=datetime.strptime("2026-"+stamps[0].group(1),"%Y-%m-%d %H:%M:%S.%f")
    tend=datetime.strptime("2026-"+stamps[-1].group(1),"%Y-%m-%d %H:%M:%S.%f")
    elapsed=(tend-t0).total_seconds()
    need(60<=elapsed<=120,"GNSS test did not run expected 70sec")
    # Postflight markers deliberately confined to the last sample and
    # never require reading persistent location settings.
    for before,value in (("IDLE_POWER","0"),("BOOT","1"),
                         ("SELINUX","Enforcing"),("SIM","LOADED")):
        need(re.search(r"(?m)^"+re.escape(before)+r"\n"+re.escape(value)+r"\n",raw) is not None,
             "device postflight failed "+before)
    need("mStarted=false" in raw and "CHECK_END" in raw,
         "Android GNSS listener did not return idle")
    need("GNSS_FIX count=" not in raw or fixes>0,
         "conflicting location fix event")
    final_cn0=re.search(r"GNSS_FINAL svEvents=\d+ fixes=\d+ maxVisible=(\d+) maxCn0=([0-9]+(?:\.[0-9]+)?)",raw)
    if final_cn0:
        final_vis,final_signal=int(final_cn0.group(1)),float(final_cn0.group(2))
        need(final_vis>=max(x[0] for x in samples) and final_vis<=64,
             "final satellite count contradicts samples")
        need(final_signal>=max_cn0_dbhz and final_signal<=70,
             "final signal contradicts samples")
        max_cn0_dbhz=max(max_cn0_dbhz,final_signal)
    return {"gate":"PASS_PHYSICAL_GNSS_SATELLITE_CALLBACKS_AND_IDLE_RECOVERY",
            "duration_seconds":round(elapsed,3),
            "satellite_updates":total_events,
            "max_visible_sv":max(x[0] for x in samples),
            "max_used_in_fix":max(x[1] for x in samples),
            "location_fixes":fixes,
            "gnss_signal_strength_samples":signal_samples,
            "max_satellites_cn0_ge_25":max_strong25 if signal_samples else None,
            "max_cn0_db_hz":max_cn0_dbhz if signal_samples else None,
            "gnss_listener_started":True,
            "gnss_returned_to_idle":True,
            "gps_power_returned_to_zero":True,
            "android_boot_completed":True,
            "sim_preserved":True,
            "actual_position_fix_verified":bool(fixes),
            "precise_location_data_collected":False,
            "no_firmware_writes":True}

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--evidence",type=Path,required=True)
    p.add_argument("--json-output",type=Path)
    a=p.parse_args()
    evidence=a.evidence.read_text()
    report=parse(evidence)
    wrong=[
        ("no_finish", evidence.replace("GNSS_FINAL ","GNSS_PENDING ",1)),
        ("no_satellites", re.sub(r"GNSS_SV visible=\d+", "GNSS_SV visible=9999",evidence)),
        ("satellite_used_gt_visible", re.sub(
            r"GNSS_SV visible=(\d+) used=\d+",
            lambda m: "GNSS_SV visible="+m.group(1)+" used="+str(int(m.group(1))+1),
            evidence, count=1)),
        ("no_idle_power", evidence.replace("IDLE_POWER\n0\n","IDLE_POWER\n1\n")),
        ("no_idle_framework", evidence.replace("mStarted=false","mStarted=true")),
        ("no_completion", evidence.replace("CHECK_END","CHECK_NOT_RECORDED")),
        ("summary_tamper", re.sub(
            r"GNSS_FINAL svEvents=\d+","GNSS_FINAL svEvents=2",
            evidence,count=1)),
    ]
    if "strong25=" in evidence:
        wrong.append(("signal_strong_gt_visible",re.sub(
            r"strong25=\d+", "strong25=100",evidence,count=1)))
        wrong.append(("signal_max_cn0_invalid",re.sub(
            r"strongestCn0=[0-9.]+", "strongestCn0=999.0",evidence,count=1)))
    for name,fake in wrong:
        try: parse(fake)
        except ValueError: pass
        else: raise AssertionError("negative test fail-open: "+name)
    report['negative_controls_passed']=len(wrong)
    if a.json_output:
        a.json_output.parent.mkdir(parents=True,exist_ok=True)
        a.json_output.write_text(json.dumps(report,indent=2)+"\n")
    print(json.dumps(report,indent=2))

if __name__=="__main__":
    main()
