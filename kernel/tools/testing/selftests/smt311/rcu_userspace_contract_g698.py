#!/usr/bin/env python3
"""G698 read-only Samsung Android 11 / ARMv7 device interface contract.

Checks *real* C/DTS, Android init, ueventd and SELinux policy sources
across two local Git trees against ONE recorded passive ADB observation.
Source paths are overridable with SMT311_ANDROID_DEVICE. No ADB writes.
Not a boot or userspace ioctl/firmware validation.
"""
from pathlib import Path
import json,os
K=Path(__file__).resolve().parents[4]
A=Path(os.environ.get("SMT311_ANDROID_DEVICE",
                      "/home/debian/android/lineage-18.1-sm-t311/device/samsung/lt013g"))
COMMON=A.parent/"lt01-common"
OUT=K/"Documentation/mm/sm-t311-userspace-contract-g698.json"
FILES={
"driver":K/"drivers/misc/samsung-tab3-bcm4752.c",
"dts":K/"arch/arm/boot/dts/samsung/exynos4212-tab3-3g8.dts",
"config_on":K.parent/"build/g686-reproducible-exp-on/.config",
"config_off":K.parent/"build/g687-release-rcu-off/.config",
"radio":A/"radio/zz.t311-platform.rc",
"gps_rc":A/"gps-v195/init.lt013g.gps-legacy.rc",
"gps_xml":A/"gps/gps.xml",
"gps_manifest":A/"gps/manifest.gnss.xml",
"udev":COMMON/"rootdir/ueventd.lt013g.rc",
"file_contexts":COMMON/"sepolicy/vendor/file_contexts",
"genfs":COMMON/"sepolicy/vendor/genfs_contexts",
"gps_policy":COMMON/"sepolicy/vendor/lt013g_gps_legacy.te",
"rild_policy":COMMON/"sepolicy/vendor/lt013g_rild_access.te",
"hardware":A/"t311-platform.mk",
"device_make":A/"t311-custom-bootimg.mk",
}
PHYSICAL_PASSIVE={
"timestamp_local":"2026-10-21 05:15-05:40 Europe/Warsaw (single read-only observation, approximate)",
"boot_completed":"1","kernel":"7.3.0-pjarczak",
"selinux":"Enforcing","ril_daemon":"running",
"cpboot_daemon":"running","audio_ril":"running",
"gnss_service":"running","legacy_gps_v195":"running","sim_state":"LOADED",
"ttySAC1_label":"lt013g_gps_serial_device","ttySAC1_mode":"crw-rw----",
"ttySAC1_owner":"root","ttySAC1_group":"system",
"umts_boot0_label":"radio_device","umts_boot0_mode":"crw-rw----",
"umts_ipc0_label":"radio_device","umts_ipc0_mode":"crw-rw----",
"umts_owner":"system","umts_group":"radio",
"ttyS0_label":"serial_device","ttyS0_mode":"crw-------",
"ttyS0_owner":"root",
"binderfs_binder_label":"binder_device",
"unprivileged_gps_power_file_readable":False,
"gps_power_kernel_object_created_on_boot_proven":False,
"gnss_fix_tested":False,"ril_call_tested":False,
"usb_otg_stress_tested":False,"rcu_off_physically_booted":False,
"BOOT_p9_SHA_read_in_G698":False,
}
def gates(src,obs):
 d=src["driver"];dt=src["dts"];gps=src["gps_rc"];f=src["file_contexts"];u=src["udev"]
 r=src["radio"];gen=src["genfs"];policy=src["gps_policy"]
 cfg=src["config_off"];on=src["config_on"]
 return {
  "physical_G650_booted_and_enforcing":
      obs["boot_completed"]=="1" and obs["kernel"]=="7.3.0-pjarczak" and obs["selinux"]=="Enforcing",
  "observed_RIL_and_modem_services_running":
      obs["ril_daemon"]==obs["cpboot_daemon"]==obs["audio_ril"]=="running" and obs["sim_state"]=="LOADED",
  "observed_GNSS_and_isolated_stock_service_running":
      obs["gnss_service"]==obs["legacy_gps_v195"]=="running",
  "kernel_DTS_bcm4752_gpio_and_clock":
      'compatible = "samsung,tab3-bcm4752";' in dt and
      'clock-names = "lpo";' in dt and "enable-gpios" in dt,
  "kernel_GPS_attribute_dedicated_sysfs_power_and_0664":
      'kobject_create_and_add("GPS_PWR_EN"' in d and
      '__ATTR(value, 0664,' in d and 'sysfs_create_file(gps->power_kobj' in d,
  "stock_GNSS_same_exact_sysfs_power_path_as_kernel":
      "/sys/class/sec/gps/GPS_PWR_EN/value" in d and
      "/sys/class/sec/gps/GPS_PWR_EN/value" in gps and
      "/sys/class/sec/gps/GPS_PWR_EN/value" in src["gps_xml"],
  "GPS_serial_TTY_consistent_in_kernel_and_init":
      "uart=/dev/ttySAC1" in d and "/dev/ttySAC1" in gps and
      "/dev/ttySAC1" in u and "/dev/ttySAC1" in f,
  "GPS_serial_policy_actual_group_and_type":
      "lt013g_gps_serial_device" in f and
      "lt013g_gps_serial_device:chr_file rw_file_perms" in policy and
      "root system /dev/ttySAC1" in gps and
      obs["ttySAC1_label"]=="lt013g_gps_serial_device" and
      obs["ttySAC1_owner"]=="root" and obs["ttySAC1_group"]=="system",
  "GPS_power_genfs_and_SELinux_permit_domain_not_shell":
      "genfscon sysfs /devices/virtual/sec/gps/GPS_PWR_EN/value u:object_r:sysfs_lt013g_gps_power:s0" in gen and
      "allow lt013g_gps_legacy sysfs_lt013g_gps_power:file" in policy and
      not obs["unprivileged_gps_power_file_readable"],
  "GPS_init_uses_isolated_stock_runtime_and_seclabel":
      "service vendor.lt013g-gps-legacy-v195" in gps and
      "seclabel u:r:lt013g_gps_legacy:s0" in gps and
      "namespace mnt" in gps and "init_daemon_domain(lt013g_gps_legacy)" in policy,
  "HIDL_GNSS_manifest_v1_service":
      "<name>android.hardware.gnss</name>" in src["gps_manifest"] and
      "<name>IGnss</name>" in src["gps_manifest"],
  "XMM6262_DTS_control_and_driver_CONFIG":
      'compatible = "samsung,tab3-xmm6262";' in dt and
      "CONFIG_SAMSUNG_TAB3_XMM6262=y" in cfg and
      "CONFIG_SAMSUNG_TAB3_XMM6262=y" in on,
  "modem_device_init_permission_and_file_contexts":
      "/dev/umts*" in u and "/dev/umts.*" in f and
      "chown system radio /dev/umts_boot0" in r and
      "chown system radio /dev/umts_ipc0" in r,
  "modem_live_nodes_match_declared_RADIO_type_and_DAC":
      obs["umts_boot0_label"]==obs["umts_ipc0_label"]=="radio_device" and
      obs["umts_boot0_mode"]==obs["umts_ipc0_mode"]=="crw-rw----" and
      obs["umts_owner"]=="system" and obs["umts_group"]=="radio",
  "RIL_init_keeps_root_S0_and_stock_libsec_RIL":
      "service ril-daemon /vendor/bin/hw/rild" in r and
      "/vendor/lib/t311-ril/libsec-ril.so" in r and
      "seclabel u:r:rild:s0" in r and
      "-d /dev/ttyS0" in r and obs["ttyS0_owner"]=="root",
  "RIL_healthy_link_restart_does_not_reset_CP":
      "on property:init.svc.ril-daemon=restarting" in r and
      "stop t311-audio-ril" in r and
      "stop cpboot-daemon" not in r.split("on property:init.svc.ril-daemon=restarting",1)[1].split("service",1)[0],
  "binderfs_expected_both_kernel_and_live":
      "CONFIG_ANDROID_BINDERFS=y" in cfg and
      obs["binderfs_binder_label"]=="binder_device",
  "serial_DWC2_audio_and_GNSS_kconfigs_survive_RCU_OFF":
      all(f"CONFIG_{key}=y" in cfg for key in
          ["SERIAL_SAMSUNG","USB_DWC2","SOUND","SND_SOC",
           "SAMSUNG_TAB3_BCM4752","SAMSUNG_TAB3_XMM6262"]),
  "Android_vendor_bootimg_make_still_old_not_G696":
      "G541" in src["device_make"] and
      "G696" not in src["device_make"],
  "no_claim_of_user_space_runtime_validation_for_new_boot":
      not obs["gnss_fix_tested"] and not obs["ril_call_tested"] and
      not obs["rcu_off_physically_booted"] and not obs["BOOT_p9_SHA_read_in_G698"],
 }
def mutants(src,obs):
 edits={
  "gps_kernel_node_renamed":("driver",'kobject_create_and_add("GPS_PWR_EN"','kobject_create_and_add("BROKEN"'),
  "gps_legacy_domain_too_broad":("gps_rc","seclabel u:r:lt013g_gps_legacy:s0","seclabel u:r:shell:s0"),
  "gps_power_genfs_missing":("genfs","genfscon sysfs /devices/virtual/sec/gps/GPS_PWR_EN/value","genfscon sysfs /devices/virtual/sec/gps/BROKEN/value"),
  "gps_serial_context_wrong":("file_contexts","/dev/ttySAC1 u:object_r:lt013g_gps_serial_device:s0","/dev/ttySAC1 u:object_r:device:s0"),
  "umts_init_permissions_gone":("radio","chown system radio /dev/umts_ipc0","chown root root /dev/umts_ipc0"),
  "RIL_uses_wrong_radio_library":("radio","/vendor/lib/t311-ril/libsec-ril.so","/vendor/lib/t311-ril/other.so"),
  "modem_config_disabled":("config_off","CONFIG_SAMSUNG_TAB3_XMM6262=y","CONFIG_SAMSUNG_TAB3_XMM6262=n"),
  "binderfs_disabled":("config_off","CONFIG_ANDROID_BINDERFS=y","CONFIG_ANDROID_BINDERFS=n"),
  "unsafe_claim_device_booted":("observed","rcu_off_physically_booted",True),
 }
 caught={}
 for name,(file,old,new) in edits.items():
  if file=="observed":
   copy=dict(obs);copy[old]=new
   result=gates(src,copy)
  else:
   assert old in src[file],(name,"mutation source missing")
   copy=dict(src);copy[file]=copy[file].replace(old,new,1)
   result=gates(copy,obs)
  fail=[k for k,v in result.items() if not v]
  assert fail,(name,"mutation not caught")
  caught[name]=fail
 return caught
def main():
 files={name:path.read_text() for name,path in FILES.items()}
 good=gates(files,PHYSICAL_PASSIVE)
 assert len(good)==20 and all(good.values()),good
 bad=mutants(files,PHYSICAL_PASSIVE)
 assert len(bad)==9
 report={
   "stage":"G698",
   "date":"2026-10-21","scope":"Cross-tree source contract and ONE passive Android G650 observation, NOT a release-off boot",
   "android_device_tree_used":"device/samsung/lt013g",
   "common_sepolicy_and_ueventd":"device/samsung/lt01-common",
   "positive_source_device_contracts":good,
   "positive_count":len(good),"deliberately_broken_contracts_detected":bad,
   "physical_passive_observation":PHYSICAL_PASSIVE,
   "limitations":[
      "Single snapshot of existing physical G650: services running, no call/SMS/GNSS-fix/audio/USB test and no new BOOT.",
      "SELinux shell denial at /sys/class/sec/gps/GPS_PWR_EN/value does not demonstrate a GNSS domain denial; kernel creates a dedicated 0664 sysfs attribute and Android grants GPS domain explicit access.",
      "Static source and present device-node contexts do not prove complete Android ioctl/syscall ABI or physical RCU-OFF compatibility.",
      "No live BOOT p9 read, recovery test, backup, reboot, OTA creation, kernel flashing or PIN activity."
   ],
   "flash_authorized":False,
   "physical_G650_unchanged":True,
   "physical_RCU_OFF_tested":False,
   "four_kernel_executable_source_paths_staged_not_installed":["mm/filemap.c","mm/page_alloc.c","mm/shmem.c","mm/vma.c"]
 }
 OUT.write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n")
 print(f"G698 {len(good)}/20 Android kernel/GNSS/RIL source+physical guards PASS, {len(bad)}/9 damaged sources caught")
 print("WARNING: source+one G650 observation only; RCU OFF full G696 BOOT UNINSTALLED")
if __name__=="__main__":main()
