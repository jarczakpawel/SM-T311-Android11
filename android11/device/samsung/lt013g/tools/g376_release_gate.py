#!/usr/bin/env python3
"""G376 fail-closed stock-parity review for SM-T311 Android11 target-files.

Distinguish intentional, VERIFIED new SystemUI APK/ODEX/VDEX + build-id-only
rild from arbitrary changed files. Important: this never overrides SELinux
domain enforcing requirements and NEVER marks a candidate safe to flash.
It only reads existing audited JSON files; private binaries stay local.

Usage: g376_release_gate.py --audit G375-REBUILT-RELEASE-AUDIT.json
  --rild G375-RILD-RUNTIME-SECTIONS-PARITY.json
  --manifest G375-FULL-RELEASE-INTEGRATION-REPORT.json
  --report G376-RELEASE-GATE.json
"""
from __future__ import annotations
import argparse
import hashlib
import json
import zipfile
from pathlib import Path
import sys

SYSTEMUI={
    "/system/system_ext/priv-app/SystemUI/SystemUI.apk",
    "/system/system_ext/priv-app/SystemUI/oat/arm/SystemUI.odex",
    "/system/system_ext/priv-app/SystemUI/oat/arm/SystemUI.vdex",
}
RILD="/vendor/bin/hw/rild"
KNOWN_DELTAS=SYSTEMUI|{RILD}
EXPECTED_DOMAINS={"su","rild","lt013g_gps_legacy","backuptool"}
BOOT_SHA="f9292bab437bce12cea56e6e0c067a15c87475b69a61fc0c13fd4d3f07b9694f"
RECOVERY_SHA="f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f"

def evaluate(audit:dict,rild:dict,release:dict)->dict:
    comparison=audit["comparison"]
    different={p for p,item in comparison.items() if item["status"]=="DIFFERENT"}
    missing={p for p,item in comparison.items() if item["status"]=="MISSING"}
    matched={p for p,item in comparison.items() if item["status"]=="MATCH"}
    unknown=different-KNOWN_DELTAS
    unexpected_missing=bool(missing)
    counts=audit["counts"]
    count_consistent=(len(matched)==counts["MATCH"] and
        len(different)==counts["DIFFERENT"] and len(missing)==counts["MISSING"] and
        len(comparison)==counts["MATCH"]+counts["DIFFERENT"]+counts["MISSING"])
    has_full_intended_delta=different==KNOWN_DELTAS
    sui=audit["systemui_preopt"]
    systemui_ok=(sui["status"]=="MATCH" and
        bool(sui["apk_dex_crc32"]) and sui["apk_dex_crc32"]==sui["vdex_dex_crc32"])
    rild_ok=(rild["status"]=="PASS_RUNTIME_ALLOC_IDENTICAL_BUILD_ID_ONLY" and
        rild["section_count"]>=20 and
        rild["differing_allocated_sections"]==[".note.gnu.build-id"] and
        not rild["significant_sections_different"] and
        not rild["full_file_sha256_equal"] and
        comparison.get(RILD,{}).get("live_sha256")==rild["reference_sha256"] and
        comparison.get(RILD,{}).get("zip_sha256")==rild["candidate_sha256"])
    zip_sha=release["targetfiles_sha256"]
    boot_ok=(audit["boot_prefix"]["status"]=="MATCH" and
        audit["boot_prefix"]["target_sha256"]==BOOT_SHA and
        release["boot_live_sha_verified"]==BOOT_SHA and
        release["critical_files"]["BOOTABLE_IMAGES/boot.img"]["sha256"]==BOOT_SHA)
    rec_ok=(release["critical_files"]["BOOTABLE_IMAGES/recovery.img"]["sha256"]==
        RECOVERY_SHA and release["recovery_live_sha_verified"]==RECOVERY_SHA)
    ril_ok=(comparison.get("/vendor/lib/libril.so",{}).get("status")=="MATCH" and
        release["critical_files"]["SYSTEM/vendor/lib/libril.so"]["sha256"]==
        "719750b0f544238c0986ecbbc8c7d4b59b822f00c7ba1c883809b1b54fcee491")
    domains=set(audit["selinux_policy"]["permissive_domains"])
    all_domain_enforcing=not domains
    checks={
        "45_path_inventory_coherent":count_consistent and len(comparison)==45,
        "intentional_exact_four_deltas":has_full_intended_delta and not unknown and not unexpected_missing,
        "systemui_pair_crc":systemui_ok,
        "rild_code_data_metadata_only":rild_ok,
        "physical_boot_match":boot_ok,
        "physical_recovery_match":rec_ok,
        "safe_RIL_G368_match":ril_ok,
        "all_SELinux_domains_enforcing":all_domain_enforcing,
        "no_unexpected_policy_domains":domains.issubset(EXPECTED_DOMAINS),
        "integrated_targetfiles_manifest_not_flash_ready":release["flash_ready"] is False,
        "targetfiles_manifest_digest_defined":len(zip_sha)==64 and all(ch in "0123456789abcdef" for ch in zip_sha),
    }
    passed=sum(checks.values())
    blocked=[name for name,ok in checks.items() if not ok]
    return {
        "stage":"G376","status":"HOLD_SELINUX" if blocked==["all_SELinux_domains_enforcing"]
        else ("BLOCK_OTHER_RELEASE_GATES" if blocked else "NEEDS_LIVE_PHYSICAL_ACCEPTANCE"),
        "passed":passed,"total":len(checks),"checks":checks,
        "blocking_reasons":blocked,"remaining_permissive_domains":sorted(domains),
        "nonstock_changed_paths":sorted(different),
        "unexpected_changed_paths":sorted(unknown),
        "missing":sorted(missing),
        "targetfiles_sha256":zip_sha,
        "source_audits_are_read_only":True,
        "sim_operations":False,"flash_ready":False,
    }

def verify_target_files_bytes(path:Path,manifest:dict)->dict:
    """Rehash actual archive and 17+ pinned payload bytes, never trust JSON alone."""
    sha=hashlib.sha256()
    with path.open("rb") as f:
        for part in iter(lambda:f.read(1024*1024),b""):
            sha.update(part)
    whole_match=sha.hexdigest()==manifest.get("targetfiles_sha256")
    checked=0
    discrepancies=[]
    with zipfile.ZipFile(path) as z:
        for item,expected in manifest.get("critical_files",{}).items():
            try:
                with z.open(item) as f:
                    digest=hashlib.sha256()
                    total=0
                    for chunk in iter(lambda:f.read(1024*1024),b""):
                        digest.update(chunk);total+=len(chunk)
                if digest.hexdigest()!=expected.get("sha256") or total!=expected.get("bytes"):
                    discrepancies.append(item)
                else:checked+=1
            except KeyError:discrepancies.append(item)
    return {"target_files_sha256_actual":sha.hexdigest(),
            "target_files_sha256_ok":whole_match,
            "critical_payloads_matched":checked,
            "critical_payloads_total":len(manifest.get("critical_files",{})),
            "critical_payload_discrepancies":discrepancies,
            "byte_integrity_ok":whole_match and not discrepancies and checked>=17}

def main():
    p=argparse.ArgumentParser()
    for field in ("audit","rild","manifest"):
        p.add_argument("--"+field,type=Path,required=True)
    p.add_argument("--report",type=Path)
    p.add_argument("--target-files",type=Path,required=True)
    args=p.parse_args()
    manifest=json.loads(args.manifest.read_text())
    out=evaluate(json.loads(args.audit.read_text()),
                 json.loads(args.rild.read_text()),manifest)
    bytes_qa=verify_target_files_bytes(args.target_files,manifest)
    out["target_files_payload_integrity"]=bytes_qa
    out["checks"]["actual_zip_and_all_critical_payloads_match_manifest"]=bytes_qa["byte_integrity_ok"]
    out["total"]+=1
    if bytes_qa["byte_integrity_ok"]:out["passed"]+=1
    else:
        out["blocking_reasons"].append("actual_zip_and_all_critical_payloads_match_manifest")
        out["status"]="BLOCK_OTHER_RELEASE_GATES"
    if args.report:args.report.write_text(json.dumps(out,indent=2)+"\n")
    print("G376_RELEASE_GATE",out["status"],f"{out['passed']}/{out['total']}",
          "blocking",",".join(out["blocking_reasons"]))
    return 0 if not out["blocking_reasons"] else 2

if __name__=="__main__":sys.exit(main())
