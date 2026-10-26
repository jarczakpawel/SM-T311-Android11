# G716 — Android11 SM-T311: clean kernel selection, complete Linus/Kiryl no-ref port retired

**Actual change**: `t311-custom-bootimg.mk` now packages the CLEAN G716 offline candidate instead of the outdated G541 image. Both normal `INSTALLED_BOOTIMAGE_TARGET` and `bootimage-nodeps` validate exact 8MiB file size and SHA256 `5d275fb7977d95e9e3515ff571efe675359e0874e1f8c5aef4b9c925d19182c1`.

The 18-commit Linus/Kiryl ref-free pagecache reader implementation, ARMv7 experiment, PG_rcu_free, all dependent RCU deferred-free/writable mmap extensions and RCU-coupled FUSE counters have been removed from current kernel SOURCE (full ARM32 host build PASS). Ordinary Linux RCU, FUSE, MGLRU, HighMem, GPU, modem, GNSS, USB remain. Two independent buffered-read microoptimizations were retained, without unsafe no-ref reads.

Full G716 host build `vmlinux`/zImage/DTB PASS, and boot packer preserved G650 ramdisk, DTB and Android boot v0 layout. Kernel selftest `make -C tools/testing/selftests/smt311 check-clean-no-ref-port`: 21/21 checks and 7/7 intentionally bad inputs caught. `shmem_file_read_iter()` is 860B of ARM code versus 976B comparable historical G650; this is code size only, not measured speed.

`prebuilt/` is gitignored. To regenerate the ignored G716 BOOT prebuilt locally (not on tablet!), first run the tracked kernel builder; then from THIS directory:

```sh
python3 tools/g716-stage-clean-boot.py \
  --source /home/debian/Projekty/samsung/linux-7.3-port/artifacts/G716-CLEAN-NO-REF-PORT-HOST-ONLY/BOOT-G716-CLEAN-7.3.0-pjarczak-HOST-ONLY-DO-NOT-FLASH.img \
  --manifest tools/G716-CLEAN-BOOT-MANIFEST.json
```

This validates both SHA256 and manifest and refuses to overwrite a different prebuilt. The path is build-host-specific; for another clone provide its local artifact path. The kernel source includes `sm_t311_g716_clean_defconfig` (431 lines) and a reproducible host build script; the Git-ignored historical pinned G650 8MiB baseline required by the offline packer remains an external LOCAL prerequisite — it is not part of GitHub source. **No backup is needed or created.**

**Current physical tablet is G650, NOT G716.** The packaging recipe does NOT grant a safe flash or working recovery; p9 BOOT and p10 RECOVERY present physical SHA and owner-attended reliable TWRP path remain unverified. No risky unattended adb reboot recovery or flash.

Priorities now: P1 GNSS BCM4752/AGPS TTFF, RIL XMM6262 voice and SMS without PIN automation, OTG host vs PC MTP, ADB Wi-Fi persisted only when explicitly enabled, supervised TWRP, Mali400/HWC flicker; P2 Quick Settings location tile, Developer options lag and clean diagnostics. Normal RCU on Linux stays. Old page-ref-free experiment tools and docs are historical, not active.

S0 PASS physical G650; S1-S3 RETIRED (no-ref gates removed), S4 REMOVED; S5-S6 BLOCKED physical installation/recovery; S7 ACTIVE/PARTIAL features; S8 ACTIVE versioned code / staging manifest / table.
