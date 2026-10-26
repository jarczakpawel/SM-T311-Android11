# G700 — SM-T311 RCU-OFF BOOT retaining DEBUG_FS diagnostics, HOST ONLY

Kernel commit `9af8b55c2` tracks
`arch/arm/configs/sm_t311_rcu_off_keep_debugfs_g700.fragment`,
`tools/testing/selftests/smt311/build_g700_debugfs_rcu_off.sh`,
`tools/testing/selftests/smt311/rcu_debugfs_off_binary_g700.py`,
RST, machine JSON and S0–S8.

Compared with the host G686 experimental config matching actual G650
physical Kconfig feature selection, G700 changes EXACTLY two
Kconfig options: `CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y -> n`
and `CONFIG_PAGE_RCU_FREE=y -> disabled`, preserving
`CONFIG_DEBUG_FS=y`, all 12 critical built-in modem/GNSS/USB/ALSA
feature options and `7.3.0-pjarczak`. A fresh full ARM EABI5
GCC 16.2.0 build `vmlinux`, `zImage` and Samsung 3g8 DTB passed.
Actual global defined symbol counts: G686 26742, earlier standard
G687 RCU-OFF/DEBUG_FS=n 26614, G700 RCU-OFF/DEBUG_FS=y 26742;
all names from G686 present in G700, including ALSA/DWC2 debugfs
diagnostics. This is only a global symbol *name* comparison,
not physical module/firmware/ABI verification.

Project generator `tools/g700_pack_debugfs_rcu_off.py`
produces a COMPLETE 8MiB HOST-only Android boot v0 with exact
original G650 ramdisk and board DTB, matching previous G696
packer. Candidate path:
`linux-7.3-port/artifacts/G700-RCU-OFF-DEBUGFS-HOST-ONLY/BOOT-G700-RCU-OFF-DEBUGFS-HOST-ONLY-DO-NOT-FLASH.img`
SHA256 `aec9655acc727ab01d0bde3711414f9606c58c5fdcafb6ae5c52a2d3346edc7f`.
Kconfig+ELF+BOOT validator 21/21 actual predicates and 9/9
tampering tests PASS, full suite 32/32 G659–G700 PASS,
both prior build profiles and new full G700 build PASS.

**No Android runtime source changed, `t311-custom-bootimg.mk`
still points at historic G541 and was not repointed.**
Physical Samsung still boots G650 `7.3.0-pjarczak`, RIL running
SIM LOADED. G699 confirmed Android unprivileged shell cannot
read current raw BOOT p9 or RECOVERY p10 SHA. Original TWRP
host SHA is historically pinned only. NO flash, TWRP reboot,
backup, modem/SMS/phone, audible tests, GNSS fix or OTG tests.
This is a non-installed production candidate; compatibility
and safe recovery still BLOCKED. S0 PASS S1 ACTIVE S2–S6
BLOCKED S7 PARTIAL S8 ACTIVE. G701 next operator-attended
read-only physical identity / diagnostics decision. GitHub
pushed exclusively by owner.


**G700 replay fix:** Kernel follow-up Git `ac92bf018` now invokes the pinned project G700 8MiB BOOT generator during the reproducible host build, before binary validation; a second full HOST-only replay yielded exactly the same candidate SHA, 21/21 guards and 9/9 corrupt inputs rejected. Project/S8 report commit `c395a9f`. No system or device files changed and no physical kernel flashing.


Final tracked-Git gate reconciliation: kernel commit `9de27173d` fixes G673, G686, G687 and S0–S8 checks to whitelist ONLY the exact two G686 and G700 non-executable `arch/arm/configs` fragments. It does NOT whitelist any other changed kernel runtime files: exactly four `mm/*.c` changes remain STAGED NOT INSTALLED versus G650. After fix 32/32 full regression suite again PASS. Project S8 commit `8210408`. Physical G700 boot still not attempted.
