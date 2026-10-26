# G563 - stream SM-T311 SYSTEM in five verified parts

This utility is specific to Samsung SM-T311 with Linux7.3 Android11 and original TWRP3.1.1-0. It is a service-only helper, **not an Android app** and not included in the normal target-files package.

The exact G562 target-files has SHA256:
`b6db182c721758a3a1ee00eab32ddf40ce6458c54728f918945bef7a3ccd31f6`.

The verified raw ext4 SYSTEM is **2,235,564,032 bytes**, SHA256:
`ea9ad056e22a912224dd24d09f986d1e0e514459297fce20bbd51eb16ff3fff8`.

Five 512 MiB-or-smaller compressed raw chunks are in:
`/home/debian/Projekty/samsung/artifacts/SM-T311-G563-STREAMED-SYSTEM-20261019/`.

Check host chunks without any ADB or flash:

```bash
cd /home/debian/android/lineage-18.1-sm-t311
python3 device/samsung/lt013g/tools/t311-io64-chunk/test_sha256_local.py
python3 device/samsung/lt013g/tools/t311-io64-chunk/stage_and_flash.py /home/debian/Projekty/samsung/artifacts/SM-T311-G563-STREAMED-SYSTEM-20261019
```

The last command defaults to DRY RUN. All compressed parts and all raw parts are individually verified against the manifest; concatenated raw SHA must equal the pinned G562 full SYSTEM hash before any physical action.

If, and only if, an operator consciously decides to install the G562 candidate in original TWRP, use the exact script with `--serial <USB_ADB_SERIAL>` and `--flash-token I_AUTHORIZE_G563_RAW_SYSTEM_P20`. **This mode was NOT executed in G563.** The script rejects non-TWRP and non-root shells; mismatching 8MiB BOOT p9/recovery p10 hashes; wrong block size; mounted SYSTEM via helper; insufficient /data; bad transfer digests; and every incomplete/corrupted part. It checks source SHA before native writes and reads back and checks exact SHA on each 64-bit partition offset. The final entire p20 SHA must equal G562's raw SHA. It never writes BOOT, RECOVERY, DATA, CACHE, CP, modem, SIM or EFS. It never reboots.

G564 recovery refinement: if the physical installation stops after one or more parts, `--resume-from N` restarts at part N without rewriting parts 0 through N-1. Before any further write, it reads those earlier parts directly from p20 via the native recovery-only `verify-installed` command and requires their SHA256 to match the signed host manifest exactly; otherwise it blocks. Each newly written part also receives a fresh independent `verify-installed` check after copy. The installer rejects Wi-Fi-style serials, accepts only a local USB serial for actual recovery installation, and streams host gzip hashing to limit memory use. The native helper is statically linked and remains service-only. Offline mocks: wrong TWRP, wrong resume-part SHA, corrupted compressed transfer, plus a full simulated resume from part 2 - all PASS, with no real ADB invocation or physical writes. Source/tool bundle Git G564 supersedes the original G563 native binary; always use the updated SHA from the current handoff.

After an interrupted mid-write, **DO NOT BOOT**. Remain in TWRP and repair/finish safely, because partially written ext4 SYSTEM is not bootable. No new rollback backup is produced; do not delete preexisting backups or user files. The staging script removes only its own temporary gz/raw chunk after that chunk is verified.

The helper's first four parts are 536,870,912 bytes each, the final part 88,080,384 bytes at offset 2,147,483,648 (above 32-bit signed file offsets). G563 native code uses 64-bit pread64/pwrite64, exact SYSTEM block geometry, read-only SHA verification before write, recovery gating, and mountinfo major:minor mount protection. `m -j2 <Soong-output-path>` compiles the ARM32 static tool; ordinary installed program `t311_io64_chunk` is disabled by `installable: false`. The original G550 IO64 tool remains unchanged.

The owner also confirmed that MTP works on Debian (G564). This closes PC-side detection/basic access; the exact two-way copy operation has not been separately described. Current Android is running and reachable by BOTH USB ADB and native encrypted Wi-Fi ADB, with USB MTP active. **None of these G563 artifacts were transferred to the tablet or executed there.** The Android G562 replug recovery and secure ADB TLS fixed port 5555 exist in the HOST ROM but not physical SYSTEM. After controlled installation, verify USB replug, trusted WLAN TLS 5555, MTP host read/write and ordinary startup. Maintain the rule that MTP exposure follows the user's Android File transfer selection/permission rather than unlocking storage silently.

## G565 real 64-bit I/O host test

The optional `test_sparse_io64.py` uses the actual C transfer/hash implementation and a host-only generated derivative replacing only the TWRP and physical device-opening adapter with a sparse regular file under a temporary directory. The production native helper `copy.c` and bundled ARM32 executable are unchanged. It writes only the verified part4 data from the G562 artifact to a 2,235,564,032-byte sparse test file at absolute offset 2,147,483,648; its presence never permits writing the tablet.

```bash
python3 device/samsung/lt013g/tools/t311-io64-chunk/test_sparse_io64.py /home/debian/Projekty/samsung/artifacts/SM-T311-G563-STREAMED-SYSTEM-20261019
```

The script tests exact geometry, final segment copy/readback and SHA, one-byte corruption recognition and repair, wrong token/hash/index, non-regular symlink source refusal, missing host harness marker, and a bad-sized fake block. It deletes the temporary sparse file and raw part automatically. **This is host-only evidence of 64-bit offsets and corruption handling, not a physical write to TWRP's p20 or a modem/USB acceptance test.**
