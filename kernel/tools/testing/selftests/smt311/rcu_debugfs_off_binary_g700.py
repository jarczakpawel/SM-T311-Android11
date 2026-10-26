#!/usr/bin/env python3
"""G700 reproducible ARM32 debugfs-preserving, noref-RCU-OFF candidate.

Compare 3 linked actual host vmlinux profiles: physical-like G650
experimental ON compiled (G686, activation G692 refuses ON), original
G687 release OFF with DEBUG_FS disabled, and NEW G700 OFF with DEBUG_FS.
No root, physical BOOT, ADB, backups, flash or recovery.
"""
from pathlib import Path
import hashlib,json,subprocess,struct,re
K=Path(__file__).resolve().parents[4]
P=K.parent
ON=P/"build/g686-reproducible-exp-on"
OFF=P/"build/g687-release-rcu-off"
NEW=P/"build/g700-debugfs-rcu-off"
M=P/"artifacts/G700-RCU-OFF-DEBUGFS-HOST-ONLY/G700-DEBUGFS-OFF-EXPERIMENT-BOOT.json"
B=P/"artifacts/G700-RCU-OFF-DEBUGFS-HOST-ONLY/BOOT-G700-RCU-OFF-DEBUGFS-HOST-ONLY-DO-NOT-FLASH.img"
OUT=K/"Documentation/mm/sm-t311-rcu-debugfs-off-g700.json"
EXP=K/"arch/arm/configs/sm_t311_rcu_off_keep_debugfs_g700.fragment"
G696=P/"artifacts/G696-RCU-OFF-HOST-ONLY/G696-BOOT-PREFLIGHT.json"
CORE=("CONFIG_SAMSUNG_TAB3_BCM4752","CONFIG_SAMSUNG_TAB3_XMM6262",
      "CONFIG_USB_DWC2","CONFIG_MMC_DW","CONFIG_EXT4_FS","CONFIG_FUSE_FS",
      "CONFIG_CMA","CONFIG_ZRAM","CONFIG_SOUND","CONFIG_SND_SOC",
      "CONFIG_HIGHMEM","CONFIG_PREEMPT_RCU")
def sh(cmd):
 return subprocess.run(cmd,check=True,capture_output=True,text=True,timeout=35).stdout
def cfg(p):
 d={}
 for line in p.read_text().splitlines():
  if line.startswith("CONFIG_") and "=" in line:
   k,v=line.split("=",1);d[k]=v
  elif line.startswith("# CONFIG_") and line.endswith(" is not set"):
   d[line.split()[1]]="n"
 return d
def syms(p):
 s=sh(["arm-linux-gnueabihf-nm","-g","--defined-only",str(p/"vmlinux")])
 return {line.split()[-1] for line in s.splitlines() if len(line.split())==3}
def symbol(p,rel,name):
 s=sh(["arm-linux-gnueabihf-nm","-S",str(p/rel)])
 m=re.search(r"(?m)^[a-f\d]+ ([a-f\d]+) [Tt] "+re.escape(name)+r"$",s)
 if not m:return {"present":False}
 a=sh(["arm-linux-gnueabihf-objdump","-dr","--disassemble="+name,str(p/rel)])
 calls=re.findall(r"(?m)^\s*\w+:\s+R_ARM_(?:CALL|JUMP24)\s+(\S+)",a)
 words=re.findall(r"(?m)^\s*\w+:\s+([a-f\d]{8})\s+",a)
 return {"present":True,"bytes":int(m.group(1),16),"ARM_instructions":len(words),"CALLS":calls,"OPCODES":words}
def sha(x):return hashlib.sha256(x).hexdigest()
def gates(a,b,c,old,off,new,manifest,candidate,fragment,functions):
 changed={k:(a.get(k),c.get(k)) for k in set(a)|set(c) if a.get(k)!=c.get(k)}
 removed=old-new;added=new-old;retained=old&new
 offlost=old-off
 rest={"mmap_region":functions["mmap_region"],
       "shmem_file_read_iter":functions["shmem_file_read_iter"]}
 return {
  "G700_only_two_Kconfig_changes_vs_physically_matched_G650_profile":
   set(changed)=={"CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT","CONFIG_PAGE_RCU_FREE"},
  "G700_preserves_DEBUG_FS":a.get("CONFIG_DEBUG_FS")==c.get("CONFIG_DEBUG_FS")=="y" and b.get("CONFIG_DEBUG_FS")!="y",
  "G700_no_experimental_noref_reader":c.get("CONFIG_PAGE_RCU_FREE")!="y" and c.get("CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT")=="n",
  "G700_retains_all_core_builtin_drivers":all(c.get(x)==a.get(x)=="y" for x in CORE),
  "G700_same_Samsung_exynos_3g8_DTB_as_G650":
   sha((NEW/"arch/arm/boot/dts/samsung/exynos4212-tab3-3g8.dtb").read_bytes())=="6fb3e77075733d531abaa2f4c3fa1bf55c11a50f04cc5acc9838db5ea0f41bfb",
  "G700_kernel_7_3_pjarczak_release":
   (NEW/"include/config/kernel.release").read_text().strip()=="7.3.0-pjarczak",
  "G700_has_actual_linked_ARMv7_zImage_and_vmlinux":(NEW/"vmlinux").stat().st_size>15_000_000 and
   (NEW/"arch/arm/boot/zImage").stat().st_size>5_000_000,
  "G700_retains_original_DEBUGFS_symbol_unlike_G687": "debugfs_create_file_full" in new and "debugfs_create_file_full" not in off and "debugfs_create_file_full" in old,
  "G700_preserves_dwc2_debugfs_functions": "dwc2_debugfs_init" in new and "dwc2_debugfs_init" in old and "dwc2_debugfs_init" not in off,
  "G700_retains_ALSA_DEBUGFS_diagnostics":"snd_soc_debugfs_root" in new and "snd_soc_debugfs_root" in old and "snd_soc_debugfs_root" not in off,
  "G700_keeps_shared_real_modem_symbols":sum(x.startswith("tab3_xmm6262") for x in new&old)>=29,
  "G700_keeps_critical_exynos_media_USB_drivers":all(any(x.startswith(prefix) for x in retained)
    for prefix in ("dwc2_","dw_mci_","brcmf_","snd_soc_","exynos")),
  "G700_preserves_MORE_GLOBAL_symbols_than_G687":len(new)>len(off) and len(new&old)>len(off&old),
  "G700_no_new_external_Kconfig_modules":all(v!="m" for v in c.values()),
  "G700_source_fragment_is_explicitly_failclosed":
    "CONFIG_DEBUG_FS=y" in fragment and
    "# CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT is not set" in fragment and
    "# CONFIG_PAGE_RCU_FREE is not set" in fragment,
  "G700_actual_mmap_region_avoids_experiment_new_GP":
    rest["mmap_region"]["bytes"]==356 and
    "synchronize_rcu" not in rest["mmap_region"]["CALLS"],
  "G700_actual_shmem_read_optimized":
    rest["shmem_file_read_iter"]["bytes"]==920 and
    "filemap_read_rcu" not in rest["shmem_file_read_iter"]["CALLS"],
  "G700_manifest_sha_matches_host_boot":
    sha(candidate)==manifest.get("g700_boot_sha256") and len(candidate)==8388608,
  "G700_preserves_original_G650_ramdisk":manifest.get("baseline_ramdisk_sha256")==
    "681d59437776b09ede48dc57e16b34697d08b7161f820673b8c84df3943bd8fd",
  "G700_manifest_safety_flags_permanently_false":
    manifest.get("flash_authorized") is False and
    manifest.get("physical_boot_current_p9_sha_verified") is False and
    manifest.get("physical_recovery_current_p10_sha_verified") is False and
    manifest.get("owner_attended_recovery_preflight_passed") is False,
  "G700_actual_boot_image_android_v0":candidate[:8]==b"ANDROID!" and len(candidate)==8*1024*1024 and
    struct.unpack_from("<I",candidate,36)[0]==2048,
 }
def mutations(a,b,c,old,off,new,m,boot,f,func):
 variants={
 "enable_unsafe_RCU":(a,b,{**c,"CONFIG_PAGE_RCU_FREE":"y"},old,off,new,m,boot,f,func),
 "disable_DEBUG_FS":(a,b,{**c,"CONFIG_DEBUG_FS":"n"},old,off,new,m,boot,f,func),
 "disable_modem":(a,b,{**c,"CONFIG_SAMSUNG_TAB3_XMM6262":"n"},old,off,new,m,boot,f,func),
 "erase_GNSS_symbol":(a,b,c,old,off,new-{"tab3_xmm6262_get_state"},m,boot,f,func),
 "lose_debugfs_function":(a,b,c,old,off,new-{"debugfs_create_file_full"},m,boot,f,func),
 "pretend_flash_allowed":(a,b,c,old,off,new,{**m,"flash_authorized":True},boot,f,func),
 "change_BOOT_byte":(a,b,c,old,off,new,m,boot[:-1]+bytes([boot[-1]^1]),f,func),
 "break_mmap_GP_assumption":(a,b,c,old,off,new,m,boot,f,
      {**func,"mmap_region":{**func["mmap_region"],"CALLS":["synchronize_rcu"]}}),
 "delete_fragment":(a,b,c,old,off,new,m,boot,f.replace("CONFIG_DEBUG_FS=y",""),func),
 }
 caught={}
 for name,arguments in variants.items():
  wrong=[k for k,v in gates(*arguments).items() if not v]
  assert wrong,name
  caught[name]=wrong
 return caught
def main():
 for p in (ON,OFF,NEW):assert (p/"vmlinux").is_file(),p
 a,b,c=(cfg(p/".config") for p in (ON,OFF,NEW))
 old,off,new=(syms(p) for p in (ON,OFF,NEW))
 manifest=json.loads(M.read_text());boot=B.read_bytes();fragment=EXP.read_text()
 fns={
  "mmap_region":symbol(NEW,"mm/vma.o","mmap_region"),
  "shmem_file_read_iter":symbol(NEW,"mm/shmem.o","shmem_file_read_iter"),
  "filemap_read":symbol(NEW,"mm/filemap.o","filemap_read"),
 }
 checks=gates(a,b,c,old,off,new,manifest,boot,fragment,fns)
 assert len(checks)==21 and all(checks.values()),checks
 mutants=mutations(a,b,c,old,off,new,manifest,boot,fragment,fns)
 assert len(mutants)==9
 report={
 "stage":"G700","classification":"HOST_ONLY_RCU_OFF_DEBUGFS_PRESERVED",
 "result":checks,"positive_count":len(checks),
 "negative_mutations_caught":mutants,
 "experiment_build_defined_globals":len(old),
 "G687_debugfs_disabled_defined_globals":len(off),
 "G700_debugfs_kept_defined_globals":len(new),
 "G700_globals_shared_with_experiment":len(old&new),
 "G700_globals_lost_vs_experiment":len(old-new),
 "G700_globals_added_vs_experiment":len(new-old),
 "G700_globals_gained_vs_G687":sorted(new-off),
 "G700_remainder_missing_globals":sorted(old-new),
 "DEBUGFS_preserved":True,
 "PAGE_RCU_FREE_enabled":False,
 "SM_T311_RCU_HOT4K_EXPERIMENT_enabled":False,
 "G700_host_BOOT_sha256":sha(boot),
 "full_compiled_functions":{k:{x:v for x,v in data.items() if x!="OPCODES"} for k,data in fns.items()},
 "physical_BOOT_hash_reverified":False,
 "physical_recovery_hash_reverified":False,
 "G700_physically_booted":False,
 "flash_authorized":False,
 "limitations":["This compares full host vmlinux and source Kconfig; no live Android boot or real app performance.",
   "CONFIG_DEBUG_FS retains diagnostic availability and security exposure, but does NOT imply the no-ref RCU experiment is enabled.",
   "Live BOOT/RECOVERY raw p9/p10 identity unreadable to Android uid2000; future manually supervised TWRP check required.",
   "No backups, physical boot write, reboot, audio tests, phone calls, SIM PIN, GNSS fix or OTG testing."],
 "runtime_source_changes_staged_NOT_INSTALLED":["mm/filemap.c","mm/page_alloc.c","mm/shmem.c","mm/vma.c"]
 }
 OUT.write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n")
 print(f"G700 {len(checks)}/21 actual ARM32 config/ELF/BOOT gates PASS; {len(mutants)}/9 tampered inputs rejected")
 print("symbol counts EXP:",len(old),"G687:",len(off),"G700:",len(new),"G700 preserved:",len(old&new))
 print("G700 BOOT HOST ONLY",sha(boot))
if __name__=="__main__":main()
