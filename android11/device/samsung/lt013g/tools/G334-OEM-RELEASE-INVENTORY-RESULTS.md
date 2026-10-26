# Samsung SM-T311 G334 — reproducible OEM asset audit for GitHub release

G334 does **not** change Android 11, GNSS, the XMM6262 kernel driver, TWRP or user data. It addresses open project task **P0-5 (OEM binary redistribution and provenance)** with a deterministic, read-only evidence collector. It extends the existing checked-in device documentation `OEM_BLOBS_PUBLISHING.md` and `PRIVATE_VENDOR_INPUTS.md` instead of replacing either.

Command, from the Android 11 checkout:

```sh
python3 device/samsung/lt013g/tools/audit_oem_blob_release.py \
  --output /home/debian/Projekty/samsung/artifacts/SM-T311-G334-OEM-LICENSE-AUDIT-20261016/g334_oem_inventory.json
```

## Inventories actually inspected (all paths and hashes recorded locally)

| Input | Files | Total bytes | In device Git | Local license/NOTICE evidence in input tree |
|---|---:|---:|---:|---|
| `vendor/samsung/lt013g/proprietary` | 12 | 6,957,885 | Separate vendor tree, not the device Git | None detected |
| `vendor/samsung/lt01-common/proprietary` | 66 | 12,026,265 | Separate vendor tree, not the device Git | None detected |
| `device/samsung/lt013g/gps-v195/immutable` | 48 | 33,288,075 | 0 tracked | None detected |
| `device/samsung/lt013g/radio/prebuilt` | 5 | 2,333,284 | 0 tracked | None detected |
| `device/samsung/lt013g/prebuilt` | 1 | 8,388,608 | 0 tracked | None detected |
| **Total** | **132** | **62,994,117** | **0 tracked in device Git** for device-local private groups | No standalone licenses found in audited input subtrees |

Absence of a separate license file in these asset directories **does not prove** that an open-source license cannot apply. In particular, the 48-file Android API25 GPS rootfs contains mixed-origin Android libraries alongside Broadcom firmware and other compatibility components. These need per-file source/NOTICE correspondence and actual provenance review, not a blanket proprietary label.

Other verified read-only findings:

- Stock API25 `legacy-rootfs.tar` has **55 file entries** (48 immutable + 7 mutable), SHA256 **559c4ac19e3be326788a429c890e3d72d08aff5ca324ac1d40ad5a3c679b83ae**, exactly matching `PRIVATE_VENDOR_INPUTS.md`. The tar is **not tracked in device Git**.
- Five actual local modem prebuilt file SHA256 values match the previously tracked `PRIVATE_VENDOR_INPUTS.md` checksums. Includes original `libsec-ril.so` and `t311-cbd`. Their presence supports local reproduction, **not** a claim of distribution permission.
- The signed G333 cumulative TWRP update is **8,734,109 bytes**, SHA256 **7d36e05c3bc1eb83d74229f2567096583be9114e3869c46adc93ec94248c8b1c**. Its seven payload entries are BOOT, new policy, GPS proxy binary, GPS launcher, init rc, precompiled policy and vendor SELinux CIL. It does **not** bundle the 132-item OEM inventory wholesale. A release of this ZIP still involves distributing binaries (new HAL/BOOT), so license compliance and corresponding kernel sources/notices remain a separate review.
- Linux6.12 kernel source `COPYING` states **GPL-2.0 WITH Linux-syscall-note** for the kernel, with additional per-file SPDX licenses in `LICENSES/`; publishing a BOOT/kernel must include compliant corresponding sources and notices. This audit does not certify that all components are GPL, or that all license obligations are satisfied.
- `vendor/samsung/lt013g` and `vendor/samsung/lt01-common` exist locally as asset directories, **not Git repositories**; do not mistake their absence from the device Git for license approval.
- The `.gitignore` already excludes private `gps-v195/immutable`, both GPS legacy tar inputs, `radio/prebuilt` and `prebuilt`. The inventory tool enforces these source-only Git boundaries and checks all local SHA sums without copying binaries.

## Publication gates

| Gate | Evidence | State |
|---|---|---|
| Source-only device Git avoids importing the 48 GNSS, 5 modem and 1 recovery private files | Git index inspected | **PASS — technical boundary only** |
| Verified reproducible private inputs | Original rootfs and modem hashes matched tracked private-input guide | **PASS — input integrity only** |
| Original OEM blob redistribution grants / notices | No per-blob grant established | **OPEN** |
| Derived/third-party GNSS and RIL bridge code provenance / attribution | Needs source ownership and license check | **OPEN** |
| All ROM/OTA redistributable binaries and corresponding kernel source notices | Kernel COPYING inspected but packaged-license/rights not checked | **OPEN** |
| Clean source-only checkout builds complete hardware-functional ROM without private inputs | These private blobs are still build requirements | **NOT DEMONSTRATED** |
| Physical Enforcing, stable car GNSS, SMS ACK, voice/audio and full hardware acceptance | Staged G333 is uninstalled | **OPEN** |

**Release advice:** keep the functioning proprietary inputs on the private Debian build machine. Publish source code and extraction/build instructions after attribution/licensing review; **do not publish the OEM blob inventory as actual binary contents** or publicly release full ROM/OTA images until required rights and notices are confirmed. This is an engineering inventory, not legal clearance.

G333 physical status unchanged: G301 Android11, Linux6.12.112-PJARCZAK+, SELinux Permissive. GPS+GLONASS hardware has genuine measurable C/N0 but no stable automotive fix; stock Samsung SMS reached the recipient but proprietary `libsec-ril.so` returned timeout120s rather than a correct ACK; complete user-driven phone/audio checks remain pending. G334 is audit/documentation only, **no new G334 BOOT or TWRP ZIP**.
