SM-T311 G701 — DEBUG_FS compiled is not DEBUG_FS mounted
===========================================================

Evidence and reason for this additional stage
---------------------------------------------
G700 full-linked a safer RCU-OFF Android11 kernel for Samsung
SM-T311 which keeps CONFIG_DEBUG_FS=y, preserving all 26,742
globally defined linked names of the physically G650-like
G686 config comparator. G687/G696 release-off with
CONFIG_DEBUG_FS=n lost 128 names. G701 checks the
operational meaning of this distinction against a passive
physical G650 snapshot, rather than suggesting debugfs
diagnostics are already exposed or useful at runtime.

Physical observed facts (G650, read only)
----------------------------------------
On the actually running Android 11 kernel `7.3.0-pjarczak`,
ADB shell is uid 2000 and `u:r:shell:s0`, SELinux Enforcing,
Android userdebug `ro.debuggable=1`. `/proc/config.gz`
has `CONFIG_DEBUG_FS=y`, `CONFIG_DEBUG_FS_ALLOW_ALL=y`
and `CONFIG_PAGE_RCU_FREE=y` with
`CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y`. No assertion
is made about the physical G650 runtime RCU static key.

`/proc/filesystems` exposes `nodev debugfs` and
`nodev securityfs`. The `/sys/kernel/debug` directory
exists, and `/d` is a symlink to that path. However
the directory is observed as an EMPTY root-owned sysfs-labeled
directory, and the `mount` output contains NO mounted
debugfs/tracefs. Therefore the running G650 has debugging
support COMPILED in but no debugfs mount visible through
this shell. `/proc/self/mountinfo` was not readable in
this shell (`Invalid argument`); this is NOT a full
privileged filesystem namespace inventory.

`/proc/partitions` is denied (`Permission denied`) and
p9/p10 sysfs uevent metadata are also denied. Together
with G699 denial of raw p9/p10 hashing/size, no physical
BOOT/RECOVERY identity was established. Do NOT infer that
an empty /sys/kernel/debug means a broken driver or that
a normal Android shell should acquire permission to mount.

Host compiled evidence
----------------------
- Both physically profile-matched G686 EXP and G700 OFF
  have `CONFIG_DEBUG_FS=y`, `CONFIG_DEBUG_FS_ALLOW_ALL=y`,
  `CONFIG_SECURITY_SELINUX=y`, `CONFIG_KALLSYMS=y`.
  `ALLOW_ALL` allows the kernel debugfs API to register
  normal nodes; it does NOT override Android DAC/SELinux
  for ordinary processes.
- G700 has `CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=n`,
  `CONFIG_PAGE_RCU_FREE` disabled/absent, whereas G686
  compiles the unproven reference-free experiment.
- G700 and G686 have exactly 26,742 matching globally
  defined symbol *names* in real ARMv7 vmlinux; G687
  DEBUG_FS=n has 26,614. Three lost G687 functions
  `debugfs_create_file_full`, `dwc2_debugfs_init`,
  `snd_soc_debugfs_root` are present in G700.
- A source scan of Android Samsung device and common init
  files does NOT contain an unconditional device-specific
  `mount debugfs` command. Android
  `lt01-common/sepolicy/vendor/lt013g_init_filewatcher.te`
  does carry two narrowly targeted existing allow rules
  for `init` and `debugfs_wifi_tracing`. It does not
  introduce arbitrary access by `shell`. This scan is
  NOT an exhaustive scan of the entire Android platform
  init tree.

The physical G650 still runs RIL and GNSS HAL, SIM LOADED.
NO mount, root/SELinux change, recovery, reboot, flash, backup,
GPS fix, phone call, sound or USB OTG test was performed.

Executable regression
---------------------
`tools/testing/selftests/smt311/rcu_debugfs_mount_contract_g701.py`
reads actual three host ARM32 .config + linked ELF symbol sets,
actual Samsung source policy and device .rc files, and compares
those against the explicitly documented single passive G650
observation. It asserts 19/19 predicates, and rejects 9/9
intentionally altered config/symbol/runtime/source inputs.
Machine report: `Documentation/mm/sm-t311-rcu-debugfs-runtime-g701.json`.

G700 is still HOST ONLY; its fresh physical debugfs state
cannot be known without booting it. No new physical speedup,
hardware compatibility, recovery access or security guarantee
is established by this source+physical-old-kernel contract.

Recommendation and outstanding blocks
-------------------------------------
KEEP DEBUG_FS=y in the preflight G700 candidate for on-demand,
access-controlled diagnostics, with no new Android default mount
or generalized SELinux `shell` grant. A future device decision
must address the expanded DEBUG_FS kernel API exposure while
retaining the safer no-ref RCU OFF. Physical p9 BOOT and p10 TWRP
hash/readback/rollback remain blocked from uid2000 shell;
only owner-supervised existing TWRP read-only preflight can
close the first identity gap. Unverified G700 must NOT flash.

S0 PASS, S1 ACTIVE, S2 BLOCKED, S3 BLOCKED, S4 BLOCKED,
S5 BLOCKED, S6 BLOCKED, S7 PARTIAL, S8 ACTIVE.
