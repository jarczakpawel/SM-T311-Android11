#!/usr/bin/env python3
"""G697: static built-in-driver and global-symbol eligibility for release OFF.

Grounded in real paired ARM32 host vmlinux and recorded passive Android
observations. This is NOT a test of Android syscalls/device nodes, kernel
boot, modem audio, GNSS or power transitions; NEVER flashes a tablet.
"""
from pathlib import Path
import hashlib,json,subprocess

K=Path(__file__).resolve().parents[4]
ON=K.parent/"build/g686-reproducible-exp-on"
OFF=K.parent/"build/g687-release-rcu-off"
IMAGE=K.parent/"artifacts/G696-RCU-OFF-HOST-ONLY/BOOT-G696-RCU-OFF-8MiB-HOST-ONLY-DO-NOT-FLASH.img"
DIFF=K.parent/"G696_RELEASE_OFF_CONFIG_DIFF.json"
OUT=K/"Documentation/mm/sm-t311-builtin-abi-g697.json"
OBSERVED={
    "physical_G650_kernel":"7.3.0-pjarczak",
    "physical_CONFIG_MODULES":"y",
    "physical_CONFIG_MODVERSIONS":"n",
    "physical_proc_modules_lines_observed":0,
    "physical_ko_files_in_readable_standard_vendor_system_odm_product_paths_observed":0,
    "physical_RIL_service":"running",
    "physical_SIM_state":"LOADED",
    "device_nodes_accessibility_limited_by_shell":True,
}
KEYS=[
    "CONFIG_SAMSUNG_TAB3_BCM4752","CONFIG_SAMSUNG_TAB3_XMM6262",
    "CONFIG_USB_DWC2","CONFIG_MMC_DW","CONFIG_EXT4_FS",
    "CONFIG_FUSE_FS","CONFIG_CMA","CONFIG_ZRAM",
    "CONFIG_SOUND","CONFIG_SND_SOC","CONFIG_HIGHMEM",
    "CONFIG_PREEMPT_RCU"]
CRITICAL_PREFIXES=["tab3_xmm6262","dwc2_","dw_mci_","brcmf_","snd_soc_","exynos"]

def config(p):
    rows=p.read_text().splitlines()
    found={}
    for line in rows:
        if line.startswith("CONFIG_") and "=" in line:
            key,value=line.split("=",1)
            found[key]=value
        if line.startswith("# CONFIG_") and line.endswith(" is not set"):
            found[line.split()[1]]="n"
    return found

def symbol_names(root):
    output=subprocess.run(
        ["arm-linux-gnueabihf-nm","-g","--defined-only",str(root/"vmlinux")],
        check=True,capture_output=True,text=True,timeout=40).stdout
    return {row.split()[-1] for row in output.splitlines()
            if len(row.split())==3}

def analyze(cfg_on,cfg_off,old,new,config_diff,obs,bootbytes):
    removed=old-new
    added=new-old
    preserved=old&new
    important={prefix:sorted([name for name in preserved if prefix in name.lower()])
               for prefix in CRITICAL_PREFIXES}
    disappeared={prefix:sorted([name for name in removed if prefix in name.lower()])
                 for prefix in CRITICAL_PREFIXES}
    guards={
      "actual_physical_modules_feature_compiled":
         obs["physical_CONFIG_MODULES"]=="y",
      "actual_physical_modules_observed_empty":
         obs["physical_proc_modules_lines_observed"]==0,
      "actual_device_no_visible_standard_ko_files":
         obs["physical_ko_files_in_readable_standard_vendor_system_odm_product_paths_observed"]==0,
      "both_configs_MODULES_enabled":
         cfg_on.get("CONFIG_MODULES")=="y" and cfg_off.get("CONFIG_MODULES")=="y",
      "neither_build_has_any_modular_Kconfig_driver":
         all(v!="m" for v in cfg_on.values()) and
         all(v!="m" for v in cfg_off.values()),
      "neither_module_symbol_CRC_protection_compiled":
         cfg_on.get("CONFIG_MODVERSIONS")=="n" and
         cfg_off.get("CONFIG_MODVERSIONS")=="n" and
         obs["physical_CONFIG_MODVERSIONS"]=="n",
      "no_external_host_KO_artifacts":
         not list(ON.rglob("*.ko")) and not list(OFF.rglob("*.ko")),
      "all_12_key_hardware_Kconfig_symbols_builtin":
         len(KEYS)==12 and all(cfg_on.get(name)==cfg_off.get(name)=="y" for name in KEYS),
      "config_diff_matches_29_G696_items":
         config_diff.get("different_kconfig_options_count")==29 and
         len(config_diff.get("different_kconfig_options",{}))==29,
      "all_release_OFF_defined_global_symbols_also_in_experiment":
         len(added)==0 and len(new)>26000,
      "shared_defined_globals_exact_count":
         len(preserved)==26614,
      "old_compiled_only_defined_globals_exact_count":
         len(removed)==128,
      "all_crucial_built_in_symbol_families_still_present":
         all(len(important[p])>=3 for p in CRITICAL_PREFIXES),
      "no_removed_symbols_from_modem_and_gnss_adjacent_families":
         all(not disappeared[p] for p in ("tab3_xmm6262","brcmf_","dw_mci_","exynos")),
      "limited_debugfs_removals_expected":
         "debugfs_create_file_full" in removed and
         "dwc2_debugfs_init" in removed and
         "snd_soc_debugfs_root" in removed,
      "release_OFF_disables_optional_experimental_reader":
         cfg_off.get("CONFIG_PAGE_RCU_FREE")!="y" and
         cfg_off.get("CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT")!="y",
      "G696_completed_8MiB_candidate_still_exact":
         len(bootbytes)==8388608 and
         hashlib.sha256(bootbytes).hexdigest()=="ba9aed97fe7a028fc9ff5b540c916b251a7cd064a9fbf4055b5eb3f7af254204",
    }
    return guards,important,disappeared,removed,added

def mutations(a,b,old,new,cfg,obs,boot):
    modifications={
      "invent_external_modem_modules":(a,b,old,new,cfg,{**obs,"physical_proc_modules_lines_observed":3},boot),
      "invent_external_KO_file":(a,b,old,new,cfg,{**obs,"physical_ko_files_in_readable_standard_vendor_system_odm_product_paths_observed":2},boot),
      "turn_XMM6262_into_module":(a,{**b,"CONFIG_SAMSUNG_TAB3_XMM6262":"m"},old,new,cfg,obs,boot),
      "turn_bcm4752_OFF":(a,{**b,"CONFIG_SAMSUNG_TAB3_BCM4752":"n"},old,new,cfg,obs,boot),
      "remove_writable_modem_SYMBOL":(a,b,old,new-{"tab3_xmm6262_get_state"},cfg,obs,boot),
      "add_unexpected_defined_symbol":(a,b,old,new|{"new_symbol_not_in_old_source"},cfg,obs,boot),
      "enable_modversions_in_release":(a,{**b,"CONFIG_MODVERSIONS":"y"},old,new,cfg,obs,boot),
      "disable_kernel_modules_in_release":(a,{**b,"CONFIG_MODULES":"n"},old,new,cfg,obs,boot),
      "change_candidate_BOOT_SHA":(a,b,old,new,cfg,obs,boot[:-1]+bytes([boot[-1]^1])),
    }
    caught={}
    for label,args in modifications.items():
        gates,*_=analyze(*args)
        failed=[k for k,good in gates.items() if not good]
        assert failed,(label,"invalid source/config/model evaded validation")
        caught[label]=failed
    return caught

def main():
    cfg_on=config(ON/".config")
    cfg_off=config(OFF/".config")
    syms_on=symbol_names(ON)
    syms_off=symbol_names(OFF)
    cfg_diff=json.loads(DIFF.read_text())
    boot=IMAGE.read_bytes()
    gates,kept,lost,missing,extra=analyze(
        cfg_on,cfg_off,syms_on,syms_off,cfg_diff,OBSERVED,boot)
    assert len(gates)==17 and all(gates.values()),gates
    caught=mutations(cfg_on,cfg_off,syms_on,syms_off,cfg_diff,OBSERVED,boot)
    assert len(caught)==9
    report={
      "stage":"G697",
      "scope":"Static driver Kconfig, host-defined vmlinux symbols and a single physical passive observation. NOT runtime ABI equivalence",
      "positive_guards":gates,
      "positive_count":len(gates),
      "altered_input_controls_caught":caught,
      "physical_observation":OBSERVED,
      "total_old_compiled_defined_global_symbols":len(syms_on),
      "total_new_release_OFF_defined_global_symbols":len(syms_off),
      "shared_symbols":len(syms_on&syms_off),
      "removed_from_release_OFF_count":len(missing),
      "removed_from_release_OFF":sorted(missing),
      "new_in_release_OFF_count":len(extra),
      "retained_critical_driver_symbol_prefix_counts":{k:len(v) for k,v in kept.items()},
      "missing_critical_driver_symbol_prefixes":lost,
      "key_Kconfig_features_BUILTIN":KEYS,
      "release_candidate":str(IMAGE),
      "candidate_SHA256":hashlib.sha256(boot).hexdigest(),
      "physical_p9_sha_freshly_verified":False,
      "device_booted_with_release_OFF":False,
      "dynamic_RIL_and_GNSS_compatibility_proven":False,
      "full_kernel_userspace_IOCTL_ABI_certified":False,
      "physical_flash_allowed":False,
      "limitations":[
          "G650 has no modules loaded in one observed /proc/modules read. No .ko files were seen in accessible standard Android vendor/system/odm/product paths; inaccessible paths could still contain data.",
          "Both host profiles have CONFIG_MODULES=y but zero Kconfig =m choices, and CONFIG_MODVERSIONS=n. A future Android module install or nonstandard filesystem path invalidates these assumptions.",
          "Global symbols are linker names, NOT ELF symbol version CRC or Android ioctl/sysfs/devfs API contracts.",
          "The 128 removed names include debugfs/diagnostic functions and some timekeeping helpers; runtime consequences need review.",
          "Preserving 26614 names and BCM/XMM/DWC2/etc Kconfig does NOT prove a bootable kernel nor live modem/GNSS/audio performance.",
          "Still no flash, TWRP installer, reboot, backup or user-interaction audio tests."
      ],
      "staged_runtime_sources":["mm/filemap.c","mm/page_alloc.c","mm/shmem.c","mm/vma.c"],
    }
    OUT.write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n")
    print("G697",len(gates),"/17 module ABI host guards PASS,",
          len(caught),"/9 mutants detected")
    print("symbols old",len(syms_on),"release OFF",len(syms_off),
          "shared",len(syms_on&syms_off),"missing",len(missing),"new",len(extra))
    print("crucial built-in symbol names", {k:len(v) for k,v in kept.items()})
if __name__=="__main__":main()
