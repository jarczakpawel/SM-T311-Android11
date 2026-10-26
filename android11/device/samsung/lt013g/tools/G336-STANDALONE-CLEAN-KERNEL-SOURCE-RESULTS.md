# SM-T311 G336 — independent, source-only Linux 6.12.112 Git root

**Engineering result:** a separately constructed Git repo with one new root commit and **exactly seven audited OEM firmware/regulatory assets excluded from its tree and reachable history**. The active Android 11, Linux kernel development Git history, working tree, local modem/GNSS/Wi-Fi firmware inputs and physical SM-T311 tablet are unchanged. **This is not a claim that all included source code or ROM binaries are legally cleared for public release. Do not push automatically.**

## Actual result

| Test | Actual result |
|---|---|
| Original Linux6.12.112 kernel Git source commit | `4c02b9c9b8b044c1dee71d62d9c2d6f9c0971e85` |
| Independently authored source-only ROOT commit (no parent) | `e15503a65610b35fed17a37738a9488ab823709a` |
| Original tracked paths | **86,776** |
| Isolated source-only tracked paths | **86,769** |
| Differences | **exactly 7 known G335 embedded Broadcom/Exynos firmware/regulatory files** |
| Root commit count | **1**, without links to old OEM-containing commit history |
| Linux ARM32 defconfig, Exynos DTS, modem XMM6262 driver, COPYING/Makefile blob IDs | **exact match** to original source |
| Seven original firmware blob objects present in independent repo? | **NO** — all tested by `git cat-file -e` and return not found |
| Independent repo borrowing original Git alternates? | **NO** |
| Independent `git fsck --full --no-reflogs` | **PASS** |
| New combined device+kernel technical source-path gate | **PASS** |
| Existing ORIGINAL kernel Git public release gate | **BLOCK**, expected, unchanged |
| Legal grants and notices / redistribution of full ROM/OTA | **NOT CLEARED** |

Independent local bare Git repository, on Debian:

`/home/debian/Projekty/samsung/artifacts/SM-T311-G336-CLEAN-KERNEL-SOURCE-20261016/kernel-source-only-standalone.git`

Actual independent pack: `274,719,147 bytes`, SHA256 `2be1b8ba8b36497249ab8851d27c7635f9760d688c0dc30267a909d658c8effb`. Repo occupies approximately 265 MiB.

Machine-readable reproducibility and source blob SHA report:

`/home/debian/Projekty/samsung/artifacts/SM-T311-G336-CLEAN-KERNEL-SOURCE-20261016/kernel-source-only-git-metadata.json`

Manifest SHA256 `398d65ca193542355dca3580e2abc58a9dc65852eb166eeb091839ac4e3323eb`.

## How it was built, and how to verify

1. `tools/prepare_g336_kernel_publication_snapshot.py` created a **separate** local bare Git root with an alternate *read-only* reference to the original object database. Git's separate index loaded the original HEAD tree, excluded exactly the seven validated source paths, wrote a new tree, then `commit-tree` created a fresh parentless commit. The original kernel repository was never rewritten.
2. Standalone independent transport was created using `git clone --bare --no-local` from the isolated root to `kernel-source-only-standalone.git`. This cloned **only reachable objects**, with no Git alternates. The independent object pack has been checked: the original seven excluded firmware blob IDs cannot be accessed by `git cat-file -e`.
3. `tools/test_g336_standalone_kernel_snapshot.py` rechecks exact paths/commit parent count/source OIDs, seven excluded blob IDs, Git object integrity and both repositories' publication path gate.
4. `tools/verify_source_release_gate.py` was improved to inspect both normal worktree Git indices and bare repositories' HEAD trees and reachable histories. On original device + independent kernel it returns `PASS`, while on original device + original kernel it still correctly returns `BLOCK`/exit2.

Run from Android 11 checkout:

```sh
python3 device/samsung/lt013g/tools/test_g336_standalone_kernel_snapshot.py
python3 device/samsung/lt013g/tools/verify_source_release_gate.py \
  --kernel /home/debian/Projekty/samsung/artifacts/SM-T311-G336-CLEAN-KERNEL-SOURCE-20261016/kernel-source-only-standalone.git
```

**Important limitations:** Git path/history scanning checks known risky content, not all possible binary-containing source blobs or remote refs. `git fsck` means internal integrity, not an external reproducible toolchain build and not a license audit. The seven excluded files must still be supplied from lawfully acquired OEM sources for complete Wi-Fi/Bluetooth/MFC/regulatory functionality. Not all binary-ish files are proprietary, and not all text files have automatically redistributable rights. The active G333 full ROM/OTA also has binary distribution and corresponding-kernel-source/notice obligations. P0-5 remains OPEN pending verified grants and relevant notices/source.

**Incident during tooling:** The first local scratch bare repo silently failed to remove entries because `git -C` was in a subdirectory of the temporary worktree, so relative Git pathspecs missed all seven files. Strict 86,776 → 86,769 verification **rejected** that scratch commit. The index operations were corrected to run in the temporary worktree root; the invalid scratch repo and two G336-created indexes were cleaned after strict identity checks. Original development Git, firmware and tablet were never changed.

## Hardware status continuity

Latest built signed, physically staged candidate remains **G333**, not G336. Physically LIVE remains **G301**, Android11 Linux6.12.112-PJARCZAK+ SELinux **Permissive**. G333 includes GPS+GLONASS GSV/GSA callbacks, NTP/UTC handoff, private Broadcom GPS config, RMC date rollover correction, G330–333 SUPL session race fixes and kernel XMM6262 HSIC resilience, but is uninstalled. Real GPS+GLONASS positive C/N0 was verified at a closed window; stable automotive GNSS, internal GPS date, SUPL and Android satellite callbacks remain unproven. The user-sent SMS reached the recipient but stock Samsung proprietary RIL timed out waiting for SEND_SMS positive ACK after120s; never fabricate ACK or automatically resend. Actual complete bidirectional calls, speaker/earpiece/wired/BT SCO microphones/mute and audio restoration also require user-led physical validation. No reboot, flashing, phone call, SMS or production partition writes occurred in G336.
