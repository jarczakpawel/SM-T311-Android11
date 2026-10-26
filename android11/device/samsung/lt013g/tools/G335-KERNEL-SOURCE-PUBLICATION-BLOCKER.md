# SM-T311 G335 — fail-closed source publication gate for OEM firmware in Git history

2026-10-16. **Not a ROM/BOOT/GNSS change; no physical flashing.**

## Material finding

The public-source boundary confirmed at G334 for the Android device tree is **NOT** sufficient for the separate Linux6.12 kernel repository. Direct `git ls-files`, `git rev-list --objects --all`, file sizes and SHA256 demonstrate that the kernel repository still tracks **7 device-specific firmware/regulatory/config files**, including previous Git commit objects. Two `BCM4334B0*.hcd` files are actual identical 49,855-byte Samsung/Broadcom Bluetooth controller patches.

| Kernel Git path | SHA256 (kernel) | Comparison to local OEM asset |
|---|---|---|
| `firmware/brcm/BCM4334B0.hcd` | `df6b8c588164a6be1766b9386204799a722eca291ed461a9645902412d53cde7` | **IDENTICAL** |
| `firmware/brcm/BCM4334B0.samsung,t311.hcd` | same SHA as above | **IDENTICAL** |
| `firmware/brcm/brcmfmac4334-sdio.samsung,t311.bin` | `13f31ad595656c8ed692b049c2cbc7476eae72da7e0b098ec46c1e78e9aba322` | **IDENTICAL** |
| `firmware/brcm/brcmfmac4334-sdio.samsung,t311.txt` | `edb812a43b6b3bcc67e08822a6198bbbd70250633f797ece84a928e2d5271f54` | **DIFFERENT** from vendor `6cf2c5e09f0142f6b7b0437ec1f83747ee021359541fa7a6fdb56779c3fb3139` |
| `firmware/regulatory.db` | `2fb33ca0074db573e05ef7dd50bb45b63c0ff98b7e852e1105ebad536fae8e6b` | **IDENTICAL** |
| `firmware/regulatory.db.p7s` | `c941c08f51c93e46722293b85631604c3740d86c3de0c75f79aef50d2e919179` | **IDENTICAL** |
| `firmware/s5p-mfc.fw` | `d4994caeae4b76002a9fc566dbebd5f42906f9c9b97c60b007e2474acfa15e10` | **IDENTICAL** |

Exact machine-readable JSON: `artifacts/SM-T311-G335-SOURCE-RELEASE-GATE-20261016/kernel_firmware_provenance.json`. Generated **read-only** by:

```sh
python3 device/samsung/lt013g/tools/audit_kernel_firmware_history.py \
 --output /home/debian/Projekty/samsung/artifacts/SM-T311-G335-SOURCE-RELEASE-GATE-20261016/kernel_firmware_provenance.json
```

The kernel's historical import `73bafd6e7` includes these binary file paths, alongside later upstream imports. Merely adding their paths to `.gitignore` or deleting the files in a new commit **DOES NOT** remove binary Git objects from already reachable commit history. The local kernel remains functionally unchanged and continues to have the OEM firmware build inputs; **no forced delete, Git history rewrite, rollback, push or flash**.

## Technical preventive gate, with strict source tests

New `tools/verify_source_release_gate.py` inspects Git **index** and reachable history (`git rev-list --objects --all`) of the separate SM-T311 device and kernel repositories. Excluded classes cover private GNSS API25 immutable subtree/rootfs tar, Samsung modem/radio prebuilt folder, stock recovery, vendor proprietary paths, common known OEM binary names, and kernel embedded Broadcom/MFC/regulatory binary + device-specific NVRAM config paths.

```sh
python3 device/samsung/lt013g/tools/verify_source_release_gate.py
```

Observed behavior on **actual** current checkout: **exit code 2, BLOCK PUBLIC KERNEL GIT RELEASE** because seven tracked firmware paths exist; Android device tree itself passes. Do not mark the overall publication gate green. `test_g335_source_release_gate.py` exercises **26 explicit source/binary path cases and 2 actual-repo Git index/history cases**; all 28 case assertions PASS, including recognition that kernel status MUST be blocked.

This is conservative filtering. `regulatory.db` and `regulatory.db.p7s` may come from an upstream open wireless-regulatory distribution with their own rights; similarly some legacy Android files can have open-source licenses. The audit finds **unverified provenance** rather than asserting each file is unlawful to distribute. Review exact license grants, sources and notices before deciding which files belong in a public snapshot.

## Safe publication work remaining (P0-5 is OPEN)

- For **source-only** GitHub publication, curate a clean kernel Git history that **never contains** OEM binary objects, with hardware-specific firmware retrieved from legally sourced private inputs when building. Do this in a separate clean publication checkout, not by rewriting the active development history behind existing build/test results.
- Alternative: prove the rights/required notices for each file and retain only components with verified permission. Run the gate on the actual publishable refs after any clean source packaging.
- Kernel COPYING indicates GPL-2.0 WITH Linux-syscall-note with per-file SPDX obligations; this alone does not grant redistribution rights to Broadcom/Samsung binary firmware included as files in the same repo.
- The tool checks only **reachable local Git refs**, not remote-only unfetched refs or private binaries hidden in other formats/archives. A green result would mean paths passed this technical filter, **not** complete license/compliance approval.
- Continue checking ROM/OTA binary redistribution, derived HAL bridge attribution, corresponding kernel source and required third-party notices. Public-ready ROM or OTA status is NOT established by this gate.

## Physical hardware continuity

Physical SM-T311 remains G301 Android11 Linux6.12.112-PJARCZAK+ SELinux Permissive; signed G333 cumulative GNSS+Enforcing package is staged and hash-verified but NOT installed. GPS and GLONASS C/N0 physically demonstrated, automotive lock, NTP/SUPL and satellite callbacks not yet valid; Samsung proprietary XMM6262 RIL still timed out 120 seconds despite SMS recipient-confirmed delivery. Do not fabricate SMS ACK or auto-resend. Full bidirectional calls, mic/earpiece/speaker/wired/BT SCO/audio routes remain to be tested with user. P0-1/P0-2/P0-5/P0-6/P1-10/P1-11/P1-13/P1-14/P2-18/P2-20 remain OPEN.
