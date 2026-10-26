#!/usr/bin/env python3
"""G699 Linux source-tree consumer of real host BOOT and TWRP identity gate.

A PASS means host bytes matched their pinned digests and unprivileged ADB
is correctly marked as NOT qualifying physical flash.  NOT deployment.
"""
from pathlib import Path
import hashlib,json,os,subprocess
ROOT=Path(__file__).resolve().parents[4]
PORT=Path(os.environ.get("SMT311_PROJECT_ROOT",str(ROOT.parent)))
PROJECT_GATE=PORT/"tools/g699_boot_recovery_gate.py"
MANUAL=PORT/"tools/g699_twrp_readonly_boot_identity.sh"
REPORT=PORT/"G699_BOOT_RECOVERY_HOST_PREFLIGHT.json"
OUT=ROOT/"Documentation/mm/sm-t311-boot-recovery-g699.json"
EXPECTED_REC="f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f"
EXPECTED_BOOT="2d477816fef0dbe2f41ca30747201dbe773e3dc3e710348c118226ee2999b7a3"
EXPECTED_G696="ba9aed97fe7a028fc9ff5b540c916b251a7cd064a9fbf4055b5eb3f7af254204"

def gates(rep,code,manual):
    source=rep.get("host_image_results",{})
    obs=rep.get("physical_unprivileged_ADB_observation",{})
    mutants=rep.get("tampering_controls_rejected",{})
    return {
      "host_positive_preflight_guard_count":rep.get("positive_count")==24,
      "host_positive_preflight_all_true":len(source)==24 and all(source.values()),
      "host_negative_controls_all_rejected":len(mutants)==10 and all(mutants.values()),
      "real_host_TWRP_expected_sha":rep.get("original_TWRP_recovery_HOST_IMAGE_sha256")==EXPECTED_REC,
      "real_host_G650_expected_sha":rep.get("original_G650_BOOT_host_image_sha256")==EXPECTED_BOOT,
      "real_host_candidate_G696_expected_sha":rep.get("reproducible_G696_rcu_off_HOST_ONLY_boot_sha256")==EXPECTED_G696,
      "recorded_android_shell_uid_not_root":obs.get("shell_uid")==2000 and obs.get("shell_selinux")=="u:r:shell:s0",
      "live_BOOT_hash_blocked_not_faked":not obs.get("physical_p9_current_sha_known") and
          obs.get("physical_p9_raw_sha_access")=="permission_denied",
      "live_RECOVERY_hash_blocked_not_faked":not obs.get("physical_p10_current_sha_known") and
          obs.get("physical_p10_raw_sha_access")=="not_readable_from_shell",
      "neither_partition_size_live_confirmed":not obs.get("physical_p9_current_size_known") and
          not obs.get("physical_p10_current_size_known"),
      "TWRP_not_running_and_not_rebooted":not obs.get("recovery_running") and
          not obs.get("recovery_automatic_reboot_attempted"),
      "manual_readonly_TWRP_guard_requires_real_binary":'[ -x /sbin/twrp ]' in manual and
          'id -u' in manual,
      "manual_guard_blocks_unknown_BOOT_and_REC":' || fail "LIVE BOOT differs' in manual and
          ' || fail "LIVE RECOVERY differs' in manual,
      "manual_guard_can_only_be_triggered_by_owner":rep.get("NOT_EXECUTED_TWRP") is True,
      "no_device_flash_permitted":rep.get("safe_to_flash_G696") is False,
      "no_release_off_boot_claimed":rep.get("RCU_OFF_physically_booted") is False,
      "host_code_explicitly_no_ADB_transport":
          "Never accesses ADB, raw partitions or recovery" in code and
          "import adb" not in code,
      "host_scripts_exist_as_source_code":PROJECT_GATE.is_file() and MANUAL.is_file(),
    }

def mutants(rep,code,script):
    o=rep["physical_unprivileged_ADB_observation"]
    variants={
     "fake_TWRP_readback":({**rep,"physical_unprivileged_ADB_observation":{**o,"physical_p10_current_sha_known":True}},code,script),
     "fake_BOOT_readback":({**rep,"physical_unprivileged_ADB_observation":{**o,"physical_p9_current_sha_known":True}},code,script),
     "allow_flash":({**rep,"safe_to_flash_G696":True},code,script),
     "fake_recovery_boot":({**rep,"NOT_EXECUTED_TWRP":False},code,script),
     "drop_TWRP_binary_guard":(rep,code,script.replace('[ -x /sbin/twrp ]','[ -x /bin/sh ]')),
     "accept_wrong_recovery_sha":({**rep,"original_TWRP_recovery_HOST_IMAGE_sha256":"0"*64},code,script),
    }
    caught={}
    for name,input_tuple in variants.items():
        lost=[k for k,v in gates(*input_tuple).items() if not v]
        assert lost,name
        caught[name]=lost
    return caught

def main():
    proc=subprocess.run(["python3",str(PROJECT_GATE)],cwd=PORT,capture_output=True,
                        text=True,check=True,timeout=40)
    assert "24/24" in proc.stdout and "10/10" in proc.stdout
    report=json.loads(REPORT.read_text())
    source=PROJECT_GATE.read_text();shell=MANUAL.read_text()
    positive=gates(report,source,shell)
    assert len(positive)==18 and all(positive.values()),positive
    altered=mutants(report,source,shell)
    assert len(altered)==6
    meta={
      "stage":"G699",
      "checks":positive,"positive":len(positive),
      "counterfactuals_caught":altered,
      "physical_adb_uid":2000,
      "current_live_BOOT_SHA_read":False,
      "current_live_RECOVERY_SHA_read":False,
      "original_TWRP_host_image_sha":EXPECTED_REC,
      "historical_G650_host_image_sha":EXPECTED_BOOT,
      "G696_release_OFF_host_image_sha":EXPECTED_G696,
      "manual_TWRP_test_executed":False,
      "physical_G696_booted":False,
      "flash_authorized":False,
      "s0_to_s8":"S0 PASS S1 ACTIVE S2-S6 BLOCKED S7 PARTIAL S8 ACTIVE",
      "status":"HARD_STOP_REQUIRE_REAL_PHYSICAL_READONLY_TWRP_IDENTITY",
    }
    OUT.write_text(json.dumps(meta,indent=2,ensure_ascii=False)+"\n")
    print(f"G699 KERNEL 18/18 host+unprivileged-gate predicates PASS, {len(altered)}/6 altered controls REJECTED")
    print("HOST image provenance verified; physical p9/p10 hashes NOT verified, do not flash")
if __name__=="__main__":main()
