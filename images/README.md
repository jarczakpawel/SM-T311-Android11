# Images — SM-T311 only

| File | Usage |
|---|---|
| `SM-T311-G1788-G1786-FINAL-CLEAN-ANDROID11-TWRP-OTA.zip` | **Recommended**: install in TWRP |
| `boot.img` | Verified kernel 7.3.0 + ramdisk |
| `recovery.img` | Verified TWRP 3.1.1-0 |
| `system.img` | Android **sparse** SYSTEM image |
| `target-files-G1786.zip` | Build/releasetools archive, **not flashable ZIP** |

| Verification | Value |
|---|---|
| Model | SM-T311 / lt013g (3G) |
| Signed OTA | AOSP **test keys**; RSA whole ZIP verified |
| Physical install | TWRP 3.1.1-0, exit 0 |
| Boot | Android 11 boot completed; SELinux Enforcing |
| Data | No wipe performed |
| Hashes | [SHA256SUMS.txt](SHA256SUMS.txt) |

**Install:** copy the signed OTA to SM-T311 → enter TWRP → Install → select ZIP → reboot.

**Do not** write sparse `system.img` using raw `dd`. Do not use these files on SM-T310, SM-T315 or other devices.

Large images are kept out of Git history; upload them to the repository's **Releases** as separate binary assets. Firmware included in an OTA may have third-party redistribution restrictions; verify rights before making the release public.
