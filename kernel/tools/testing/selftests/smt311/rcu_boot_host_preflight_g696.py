#!/usr/bin/env python3
"""G696: only read exact offline 8MiB G650->release-OFF BOOT candidate.

No adb, flash, backup or recovery interactions. Failure rejects BOOT
packaging; success does NOT authorize physical deployment.
"""
from pathlib import Path
import hashlib,struct,json,subprocess
ROOT=Path(__file__).resolve().parents[4]
PORT=ROOT.parent
ART=PORT/"artifacts/G696-RCU-OFF-HOST-ONLY"
BASE=PORT/"artifacts/G650-SMT311-HOTMARK-DEFAULT-OFF-BYPASS/G650-HOT4K-RCU-BOOT-8MiB.img"
CAND=ART/"BOOT-G696-RCU-OFF-8MiB-HOST-ONLY-DO-NOT-FLASH.img"
MAN=ART/"G696-BOOT-PREFLIGHT.json"
DIFF=PORT/"G696_RELEASE_OFF_CONFIG_DIFF.json"
SOURCE=PORT/"tools/g696_pack_release_off_boot.py"
EXPECTED_BASE="2d477816fef0dbe2f41ca30747201dbe773e3dc3e710348c118226ee2999b7a3"
EXPECTED_BOOT="ba9aed97fe7a028fc9ff5b540c916b251a7cd064a9fbf4055b5eb3f7af254204"
EXPECTED_RAM="681d59437776b09ede48dc57e16b34697d08b7161f820673b8c84df3943bd8fd"
EXPECTED_DTB="6fb3e77075733d531abaa2f4c3fa1bf55c11a50f04cc5acc9838db5ea0f41bfb"
EXPECTED_Z="1c2dd2e521cbfec48c95c89518487b5cfd1887027e7f9d74146027d777e86c49"
def hash_bytes(b):return hashlib.sha256(b).hexdigest()
def parts(img):
    if len(img)!=8388608 or img[:8]!=b"ANDROID!":return None
    x=struct.unpack_from("<10I",img,8)
    ks,ka,rs,ra,ss,sa,tags,page,ds,r=x
    if (ka,ra,tags,page,ss,ds)!=(0x10008000,0x12000000,0x10000100,2048,0,0):return None
    pad=lambda n:(n+2047)//2048*2048
    off=2048+pad(ks)
    if off+pad(rs)>len(img):return None
    k=img[2048:2048+ks];ram=img[off:off+rs]
    h=hashlib.sha1()
    for x in (k,ram,b""):
        h.update(x);h.update(struct.pack("<I",len(x)))
    if h.digest()!=img[576:596]:return None
    return {"kernel":k,"ram":ram,"header":img[:2048]}

def asserts(base,candidate,manifest,diff,packer):
    a=parts(base);b=parts(candidate)
    z=(PORT/"build/g687-release-rcu-off/arch/arm/boot/zImage").read_bytes()
    dt=(PORT/"build/g687-release-rcu-off/arch/arm/boot/dts/samsung/exynos4212-tab3-3g8.dtb").read_bytes()
    return {
      "G650_base_exact_sha":hash_bytes(base)==EXPECTED_BASE,
      "G696_candidate_exact_sha":hash_bytes(candidate)==EXPECTED_BOOT,
      "G696_android_v0_geometry":b is not None,
      "G650_android_v0_geometry":a is not None,
      "boot_image_8MiB":len(candidate)==8*1024*1024,
      "G650_ram_sha_expected":a is not None and hash_bytes(a["ram"])==EXPECTED_RAM,
      "G696_ram_sha_expected":b is not None and hash_bytes(b["ram"])==EXPECTED_RAM,
      "ramdisk_bytes_unmodified":a is not None and b is not None and a["ram"]==b["ram"],
      "DTB_vendor_board_pinned":hash_bytes(dt)==EXPECTED_DTB,
      "candidate_has_pinned_zImage":hash_bytes(z)==EXPECTED_Z,
      "new_kernel_is_zImage_plus_board_DTB":b is not None and b["kernel"]==z+dt,
      "G650_old_kernel_DTB_suffix":a is not None and a["kernel"].endswith(dt),
      "base_header_unchanged_except_size_and_hash":
         a is not None and b is not None and
         a["header"][12:576]==b["header"][12:576] and
         a["header"][596:]==b["header"][596:],
      "no_unsafe_config_selected":
         "CONFIG_PAGE_RCU_FREE=y" not in (PORT/"build/g687-release-rcu-off/.config").read_text() and
         "CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y" not in (PORT/"build/g687-release-rcu-off/.config").read_text(),
      "all_core_device_config_settings_remain_y":
         diff.get("all_checked_core_drivers_retained") is True and
         len(diff.get("core_hardware_feature_symbols_still_y",[]))==12,
      "source_config_delta_exactly_29":diff.get("different_kconfig_options_count")==29 and
         len(diff.get("different_kconfig_options",{}))==29,
      "manifest_flash_policy_explicitly_NO":manifest.get("flash_allowed") is False and
         manifest.get("status")=="HOST_ONLY_NOT_INSTALLED",
      "manifest_does_NOT_claim_live_p9_SHA":manifest.get("physical_sha_rechecked_directly_from_partition") is False,
      "manifest_does_NOT_claim_modem_hardware_certification":manifest.get("physical_module_ABI_and_modem_fully_verified") is False,
      "manifest_does_NOT_claim_TWRP_preflight":manifest.get("physical_TWRP_RECOVERY_and_BOOT_write_preflight_run") is False,
      "packer_uses_pinned_exact_base":f'BASE_SHA="{EXPECTED_BASE}"' in packer,
      "packer_asserts_byte_identical_G650_roundtrip":
         "pack(baseline,baseline[\"kernel\"])==original" in packer,
      "packer_has_six_corruption_controls":
         len(manifest.get("source_corruption_mutants_rejected",{}))==6 and
         all(manifest.get("source_corruption_mutants_rejected",{}).values()),
      "no_OTA_or_TWRP_payload_generated":
         manifest.get("candidate_boot_filename","").endswith("DO-NOT-FLASH.img") and
         manifest.get("format")=="Android boot v0",
    }

def mutation_controls(base,candidate,man,diff,source):
    variants={
       "changed_magic":(base,b"NOTBOOT!"+candidate[8:],man,diff,source),
       "ramdisk_corrupted":(base,candidate[:2048]+bytes([candidate[2048]^1])+candidate[2049:],man,diff,source),
       "changed_candidate_hash":(base,candidate[:-1]+bytes([candidate[-1]^1]),man,diff,source),
       "pretend_live_boot_hash_checked":(base,candidate,{**man,"physical_sha_rechecked_directly_from_partition":True},diff,source),
       "pretend_modem_certified":(base,candidate,{**man,"physical_module_ABI_and_modem_fully_verified":True},diff,source),
       "allow_flash":(base,candidate,{**man,"flash_allowed":True},diff,source),
       "lost_device_config_gate":(base,candidate,man,{**diff,"all_checked_core_drivers_retained":False},source),
       "lie_about_config_delta":(base,candidate,man,{**diff,"different_kconfig_options_count":0},source),
       "drop_original_kernel_roundtrip_check":(base,candidate,man,diff,source.replace('pack(baseline,baseline["kernel"])==original','True')),
    }
    caught={}
    for label,values in variants.items():
        failed=[name for name,valid in asserts(*values).items() if not valid]
        assert failed,(label,"undetected corrupted data")
        caught[label]=failed
    return caught

def main():
    subprocess.run(["python3",str(SOURCE),"--verify-only"],check=True,
                   capture_output=True,text=True,cwd=PORT,timeout=35)
    base=BASE.read_bytes();candidate=CAND.read_bytes()
    m=json.loads(MAN.read_text());config=json.loads(DIFF.read_text());packer=SOURCE.read_text()
    positive=asserts(base,candidate,m,config,packer)
    assert len(positive)==24 and all(positive.values()),positive
    faults=mutation_controls(base,candidate,m,config,packer)
    assert len(faults)==9
    report={
      "stage":"G696","scope":"OFFLINE BOOT packaging, never flash",
      "source_and_actual_8MiB_binary_guards":positive,
      "positive_gates":len(positive),"mutated_inputs_rejected":faults,
      "candidate_sha256":hash_bytes(candidate),
      "physical_live_boot_sha_confirmed":False,
      "flash_authorized":False,
      "RIL_GNSS_modules_runtime_compatibility_proven":False,
      "boot_process_measured":False,
      "four_source_paths_staged_not_deployed":[
           "mm/filemap.c","mm/page_alloc.c","mm/shmem.c","mm/vma.c"]
    }
    path=ROOT/"Documentation/mm/sm-t311-rcu-offline-boot-g696.json"
    path.write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n")
    print(f"G696 {len(positive)}/24 host image+device feature guards PASS, {len(faults)}/9 counterfactual failures caught; NO FLASH")
if __name__=="__main__":main()
