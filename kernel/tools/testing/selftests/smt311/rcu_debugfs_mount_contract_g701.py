#!/usr/bin/env python3
"""G701 passive G650 debugfs-vs-G700 RCU-OFF diagnostic surface contract.

No adb calls, mounts, writes to Android, root, recovery or BOOT.
Only reads real host ARM32 linked ELF/Kconfig + Android device source,
checks a recorded SINGLE passive shell snapshot (not physical G700).
"""
from pathlib import Path
import hashlib,json,re,subprocess
K=Path(__file__).resolve().parents[4]
P=K.parent
DEV=Path("/home/debian/android/lineage-18.1-sm-t311/device/samsung/lt013g")
COMMON=DEV.parent/"lt01-common"
A=P/"build/g686-reproducible-exp-on"
B=P/"build/g700-debugfs-rcu-off"
OLD=P/"build/g687-release-rcu-off"
OUT=K/"Documentation/mm/sm-t311-rcu-debugfs-runtime-g701.json"
DEVICE_RC=list(DEV.rglob("*.rc"))+list(COMMON.rglob("*.rc"))
VENDOR_RULES=COMMON/"sepolicy/vendor/lt013g_init_filewatcher.te"
PHY={
"kernel":"7.3.0-pjarczak",
"sys_boot_completed":"1",
"selinux":"Enforcing",
"adb_uid":2000,"shell_context":"u:r:shell:s0",
"ro_debuggable":"1","ro_build_type":"userdebug",
"proc_filesystems_lists_debugfs":True,
"proc_filesystems_lists_securityfs":True,
"debugfs_directory_present":True,
"debugfs_directory_type":"sysfs",
"debugfs_directory_mode":"dr-xr-xr-x",
"debugfs_listing_empty":True,
"mount_output_has_debugfs":False,
"mount_output_has_tracefs":False,
"mountinfo_access":"Invalid argument",
"proc_partitions_access":"Permission denied",
"p9_p10_uevent_access":"Permission denied",
"debugfs_explicit_mount_performed":False,
"selinux_changed":False,
"radio_service":"running",
"gnss_service":"running",
"boot_g700_installed":False,
}
CRITICAL={
"CONFIG_SAMSUNG_TAB3_BCM4752","CONFIG_SAMSUNG_TAB3_XMM6262",
"CONFIG_USB_DWC2","CONFIG_MMC_DW","CONFIG_EXT4_FS","CONFIG_FUSE_FS",
"CONFIG_CMA","CONFIG_ZRAM","CONFIG_SOUND","CONFIG_SND_SOC",
"CONFIG_HIGHMEM","CONFIG_PREEMPT_RCU"}
def config(path):
 d={}
 for row in path.read_text().splitlines():
  if row.startswith("CONFIG_") and "=" in row:
   k,v=row.split("=",1);d[k]=v
  elif row.startswith("# CONFIG_") and row.endswith(" is not set"):
   d[row.split()[1]]="n"
 return d
def syms(build):
 table=subprocess.run(["arm-linux-gnueabihf-nm","-g","--defined-only",str(build/"vmlinux")],
                     text=True,capture_output=True,check=True,timeout=40).stdout
 return {r.split()[-1] for r in table.splitlines() if len(r.split())==3}
def validate(exp,new,off,phy,on_symbols,g700_symbols,off_symbols,source,rcs,frags):
 delta={key for key in set(exp)|set(new) if exp.get(key)!=new.get(key)}
 return {
  "physical_observation_is_existing_G650":phy.get("kernel")=="7.3.0-pjarczak" and
    phy.get("sys_boot_completed")=="1" and not phy.get("boot_g700_installed"),
  "physical_userdebug_but_still_Enforcing":phy.get("ro_build_type")=="userdebug" and
    phy.get("ro_debuggable")=="1" and phy.get("selinux")=="Enforcing",
  "physical_android_ADB_unprivileged":phy.get("adb_uid")==2000 and
    phy.get("shell_context")=="u:r:shell:s0",
  "physical_debugfs_compiled_as_filesystem":phy.get("proc_filesystems_lists_debugfs") is True,
  "physical_debugfs_directory_is_empty_unmounted_sysfs":
    phy.get("debugfs_directory_present") is True and
    phy.get("debugfs_listing_empty") is True and
    phy.get("debugfs_directory_type")=="sysfs" and
    phy.get("mount_output_has_debugfs") is False,
  "physical_debugfs_not_activated_by_us":not phy.get("debugfs_explicit_mount_performed") and
    not phy.get("selinux_changed"),
  "physical_proc_partitions_still_shell_inaccessible":phy.get("proc_partitions_access")=="Permission denied" and
    phy.get("p9_p10_uevent_access")=="Permission denied",
  "physical_mountinfo_not_verified":phy.get("mountinfo_access")=="Invalid argument",
  "G700_CONFIG_DEBUG_FS_retained":exp.get("CONFIG_DEBUG_FS")==new.get("CONFIG_DEBUG_FS")=="y" and
    off.get("CONFIG_DEBUG_FS")=="n",
  "G700_DEBUG_FS_ALLOW_ALL_same_as_physical_config":exp.get("CONFIG_DEBUG_FS_ALLOW_ALL")==new.get("CONFIG_DEBUG_FS_ALLOW_ALL")=="y",
  "G700_no_reference_free_pagecache":new.get("CONFIG_PAGE_RCU_FREE")!="y" and
    new.get("CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT")=="n",
  "G700_exact_two_Kconfig_feature_delta":delta=={"CONFIG_PAGE_RCU_FREE","CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT"},
  "G700_all_12_critical_drivers_BUILTIN":len(CRITICAL)==12 and
    all(exp.get(k)==new.get(k)=="y" for k in CRITICAL),
  "G700_retains_real_DEBUG_FS_driver_symbols":
    all(s in on_symbols&g700_symbols and s not in off_symbols for s in
       ["debugfs_create_file_full","dwc2_debugfs_init","snd_soc_debugfs_root"]),
  "G700_ALL_GLOBAL_names_retained":len(on_symbols)==len(g700_symbols)==26742 and
    on_symbols==g700_symbols and len(off_symbols)==26614,
  "G700_built_in_debugfs_not_an_automatic_mount":
    phy.get("mount_output_has_debugfs") is False and
    "CONFIG_DEBUG_FS=y" in frags and
    not any(re.search(r"(?m)^\s*(?:mount\s+debugfs\b|mount\s+-t\s+debugfs\b)",x)
            for x in rcs),
  "vendor_wifi_tracing_SELinux_rule_remains_narrow":
    "allow init debugfs_wifi_tracing:dir { search write add_name };" in source and
    "allow init debugfs_wifi_tracing:file { create open write };" in source and
    "allow shell debugfs:" not in source,
  "no_physical_G700_benchmark_or_qualification":
    phy.get("boot_g700_installed") is False and phy.get("radio_service")=="running" and
    phy.get("gnss_service")=="running",
  "G700_host_image_not_runtime_mounted_debugfs_evidence":
    phy.get("debugfs_directory_type")=="sysfs" and
    not phy.get("mount_output_has_debugfs") and not phy.get("boot_g700_installed"),
 }
def mutations(exp,new,off,phy,on,now,vanilla,source,rcs,frag):
 scenarios={
  "fake_DEBUGFS_mounted":(exp,new,off,{**phy,"mount_output_has_debugfs":True},on,now,vanilla,source,rcs,frag),
  "fake_shell_root":(exp,new,off,{**phy,"adb_uid":0},on,now,vanilla,source,rcs,frag),
  "fake_physical_G700":(exp,new,off,{**phy,"boot_g700_installed":True},on,now,vanilla,source,rcs,frag),
  "disable_debugfs":(exp,{**new,"CONFIG_DEBUG_FS":"n"},off,phy,on,now,vanilla,source,rcs,frag),
  "re_enable_noref":(exp,{**new,"CONFIG_PAGE_RCU_FREE":"y"},off,phy,on,now,vanilla,source,rcs,frag),
  "disable_modem":(exp,{**new,"CONFIG_SAMSUNG_TAB3_XMM6262":"n"},off,phy,on,now,vanilla,source,rcs,frag),
  "remove_debugfs_symbol":(exp,new,off,phy,on,now-{"dwc2_debugfs_init"},vanilla,source,rcs,frag),
  "add_vendor_automatic_debugfs_mount":(exp,new,off,phy,on,now,vanilla,source,rcs+["mount debugfs debugfs /sys/kernel/debug"],frag),
  "pretend_unlimited_shell_debugfs":(exp,new,off,phy,on,now,vanilla,source+"\nallow shell debugfs:dir rw_dir_perms;",rcs,frag)
 }
 found={}
 for name,args in scenarios.items():
  failed=[k for k,v in validate(*args).items() if not v]
  assert failed,name
  found[name]=failed
 return found
def main():
 e,g,o=[config(x/".config") for x in (A,B,OLD)]
 symbol_sets=[syms(x) for x in (A,B,OLD)]
 src=VENDOR_RULES.read_text()
 vendor_inits=[x.read_text() for x in DEVICE_RC]
 fragment=(K/"arch/arm/configs/sm_t311_rcu_off_keep_debugfs_g700.fragment").read_text()
 checks=validate(e,g,o,PHY,*symbol_sets,src,vendor_inits,fragment)
 assert len(checks)==19 and all(checks.values()),checks
 altered=mutations(e,g,o,PHY,*symbol_sets,src,vendor_inits,fragment)
 assert len(altered)==9
 record={
  "stage":"G701","scope":"Host ELF+Kconfig and one single passive physical G650 debugfs/SELinux snapshot",
  "positive_count":len(checks),"positive_gates":checks,
  "mutants_rejected":altered,
  "physical_observation":PHY,
  "global_symbol_names_exp":len(symbol_sets[0]),
  "global_symbol_names_G700":len(symbol_sets[1]),
  "global_symbol_names_G687":len(symbol_sets[2]),
  "debugfs_compiled_and_filesystem_available":True,
  "debugfs_mounted_on_G650":False,
  "debugfs_G700_mounted_verified":False,
  "G700_physically_booted":False,
  "real_partition_identity_confirmed":False,
  "flash_authorized":False,
  "limitations":[
    "Physical G650 mount command shows no debugfs; /proc/self/mountinfo is not accessible to shell. Not a complete privileged mount namespace inventory.",
    "CONFIG_DEBUG_FS_ALLOW_ALL=y allows kernel APIs to create nodes, not unprivileged direct access through SELinux/DAC.",
    "device/samsung init trees scanned, not every system/core init script in platform source.",
    "No mount, writes, chmod, SELinux relaxation, root escalation, recovery, flash, backup, PIN, GNSS fix or modem/audio tests."
  ],
  "four_runtime_paths_staged":["mm/filemap.c","mm/page_alloc.c","mm/shmem.c","mm/vma.c"],
 }
 OUT.write_text(json.dumps(record,indent=2,ensure_ascii=False)+"\n")
 print(f"G701 {len(checks)}/19 host/physical snapshot checks PASS, {len(altered)}/9 mutants rejected; debugfs NOT mounted")
if __name__=="__main__":main()
