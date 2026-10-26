# SM-T311 G567 guarded low-space SYSTEM candidate (2026-10-19)

**HOST-ONLY, NOT INSTALLED.** This bundle contains five gzip-compressed raw ext4 SYSTEM fragments, an exact manifest, an ARM32 statically linked TWRP-only helper, and source-matched prepare/dry-run/optional install scripts. The physical tablet still uses its old SYSTEM, even if you see newer sources in Git.

- Target-files ROM (canonical build path): `/home/debian/android/lineage-18.1-sm-t311/out/target/product/lt013g/obj/PACKAGING/target_files_intermediates/lineage_lt013g-target_files-eng.debian.zip`.
- **G567 target-files SHA256:** `d1ddaf31688ce27f905780cd0c0638dd4887cb286041768bf0919ecc720266e1` (1,158,085,612 bytes).
- **G567 SYSTEM raw SHA256:** `73de7e72c36168204f7a08a0220318de0a0f00416a3efd50aa5e67e9ea843da0` (2,235,564,032 bytes).
- 5 chunks, first four 536,870,912 B, last 88,080,384 B at offset 2,147,483,648; 571,408,854 compressed bytes combined, peak scratch gzip + raw 802,781,130 B. Verified individually and as a reconstructed whole. Host raw image checked via read-only `e2fsck -f -n` PASS.
- ARM32 static helper pinned SHA256 `ba964378166bb4263487f85da7ae86df1f597853edde27b892f98acd789c6ff0` (same tested native helper as G564/G565), never installed in normal Android ROM.
- Original BOOT Linux7.3 p9 SHA256 `81b01abb5612a9a777330705c02c4bc1f49386da4d5df0950f955246f9fa6bce`; original TWRP p10 SHA256 `f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f`; scripts require exact SHA before p20 write.
- G567 Android source commit `frameworks/base 7e19c6e124c`: USB one-shot DWC2 recovery 6s delayed, bounded function-switch wait, no loop, preserves Android function selection. Inherits G562 secure native TLS Wi-Fi ADB port5555 with ephemeral fallback and saved trusted network, and all 18 SELinux genfs labels. 5 Mesa26 libs, OEM RIL, GPS, V191 golden audio, BOOT7.3 and G523 bounded audio bridge passed exact release gate. Does NOT include the isolated G566 experimental nonproduction audio-v191 connect changes.

## Offline validation (default; NO ADB)

```bash
cd /home/debian/android/lineage-18.1-sm-t311
D=/home/debian/Projekty/samsung/artifacts/SM-T311-G567-USB-REATTACH-SYSTEM-20261019
(cd "$D" && sha256sum -c SHA256SUMS)
python3 device/samsung/lt013g/tools/t311-io64-chunk/releases/G567/stage_and_flash.py "$D"
python3 device/samsung/lt013g/tools/t311-io64-chunk/releases/G567/test_flash_failclosed.py "$D"
```

### G568 smaller USB-ADB transfers (2026-10-19)

G550 previously needed 64 MiB pushes to TWRP because larger `adb push` transfers could disconnect. G568 improves **the installer script only**, leaving the G567 ZIP, raw SYSTEM, five compressed chunks and the native ARM32 helper unchanged. On an explicitly approved TWRP installation the launcher now streams each compressed part as **at most 64 MiB per USB `adb push`**. Each piece is SHA256-checked against the host immediately after reaching `/data/local/tmp`. The concatenated compressed stream must match the release manifest SHA256, then the decompressed raw chunk must match its raw SHA256, **before** invoking the existing C helper. This uses the same maximum compressed+raw on-device storage envelope (803 MB) as before, with no new requirement to stage the entire 2.235GB image.

The mock ADB suite now runs **5 fail-closed scenarios + 1 successful simulated resume**: wrong TWRP, previous p20 SHA mismatch, corrupted 64MiB transferred piece, corrupted recombined gzip SHA, corrupted decompressed raw SHA; successful resume-from2 writes only mocked 2,3,4 after verifying 0,1. All push piece sizes are asserted ≤64MiB. No actual ADB commands, reboot, or block writes are executed by these tests.

### G570 mandatory USERDATA p21 mount gate

Before the first raw-chunk upload, a small 32-bit static recovery helper is temporarily pushed to `/tmp` and run in **read-only `preflight-data` mode**. The helper refuses to proceed unless the kernel device id of `/data` equals the raw block-device id of `/dev/block/mmcblk0p21`, which is the Samsung USERDATA partition; it also checks that the filesystem is writable and obtains free bytes with `statvfs()`. The installer compares free bytes against the peak gzip+raw staging requirement plus 150 MiB margin, and removes the short-lived helper from `/tmp`. **It does not automatically mount /data**, and cannot accidentally stage hundreds of megabytes into TWRP RAM if /data is not properly mounted. If blocked, use TWRP's Mount interface to mount/decrypt USERDATA, verify the correct filesystem, then repeat a preflight. Do not format /data, delete backups, or bypass the mount check.

Host-only tests `test_data_mount_guard.py` reuse the exact production C validation with an isolated fake device: writable p21 mount PASS; different device, invalid block type, read-only mount and absent recovery marker all blocked (5/5). Updated release-specific mocked ADB suite checks wrong TWRP, /data unmounted, /data low space, prior written fragment mismatch, transfer corruption, compressed-hash corruption and raw-hash corruption (7 fail-closed) plus successful simulated resume (1) without physical ADB/flash. The native G570 helper is service-only and does not change the ROM.

G569 additionally verifies the COMPLETE 2,235,564,032-byte p20 SYSTEM through the same TWRP-only 64-bit `pread64` native helper, rather than depending on 32-bit BusyBox `sha256sum` to stream a greater-than-2GiB block device. The host sparse-partition regression covers the entire 2,235,564,032 bytes and succeeded with a generated SHA256 from Python's hashlib. The extra check does not change the G567 ROM or the five raw gzip parts. **G569 helper replaces old G564 SHA 8f007... in THIS G567 artifact bundle only; historical bundles are not automatically updated.**

These commands only check files and simulated ADB. Real flash mode **has not been invoked**. The physical device does not yet use TLS5555 and G567 USB reattach has not been hardware-validated.

A future operator-assisted physical install needs explicit `--serial <PHYSICAL_USB_SERIAL>` and `--flash-token I_AUTHORIZE_G567_RAW_SYSTEM_P20` with original TWRP3.1.1-0 running. It hard-blocks wrong recovery or hashes, mounted p20, insufficient free /data, bad transferred chunk, invalid helper, and mismatched previous chunk on `--resume-from N`. Always verify complete p20 raw SHA before boot. The helper's lower-level token remains `G563_FLASH_SYSTEM_P20` by design, separately checked by the script; don't rename its C protocol. Never use TWRP block-image OTA E1001, never stage the whole 2.235GB raw image inside the tablet's ~1.9GB free /data.

If an update stops after any p20 write, **DO NOT BOOT** a partially written SYSTEM; remain in TWRP and finish/repair/verify. No install script automatically reboots or changes p9/p10/CP/EFS/RECOVERY/DATA. Never overwrite user files and backups to free space.

The earlier G562 target-files ZIP SHA `b6db182c...` is historical at the canonical output path, which was overwritten by this new package. The G563/G564 artifact bundle for the older image remains separately preserved, with its own manifest and pinned install script; **never mix chunks between releases**.
