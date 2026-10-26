# SM-T311 G382 - binderfs isolation: scoped SELinux integration

2026-10-16. **Source-only - NOT physically deployed.** Current tablet remains G379; guarded G381 full OTA already staged and unchanged.

## Actual evidence from running G379

- Stock API25 Samsung GNSS launcher, bridge and gpsd exist; `android.hardware.gnss@1.0::IGnss/default` registers on Android 11.
- The launcher's actual mount namespace shows `/lt013g-gps` bind mounted at `/dev/binderfs/binder`. The Android platform's `/dev/binderfs` remains its distinct shared mount.
- Android pstore/dmesg records the following SELinux `permissive=1` denials for the GPS domain: `{ getattr setattr }` on `/dev/binderfs/lt013g-gps`, and `{ read write open ioctl }` on its private Binder node within the GNSS jail. The bind-mounted jail destination at `/data/vendor/gps/legacy-v195/rootfs/dev/binderfs/binder` requires `{ mounton }` permission on `binder_device:chr_file`.
- Stock kernel `binder.c` maintains one independent binder context for each device. The G378 T311-specific Linux 6.12 kernel provides a fourth device named `lt013g-gps` with its own Binder context. Do not create a new filesystem from the vendor domain; Android 11 `system/sepolicy/private/domain.te` explicitly neverallows this.

## Scoped source policy

- `lt01-common/sepolicy/vendor/genfs_contexts`: exact `genfscon binder /lt013g-gps`, with new device type `lt013g_gps_private_binder_device` - does not relabel `/binder`, `/hwbinder` or `/vndbinder`.
- `lt013g_g382_gps_private_binder.te`: permissions only for `lt013g_gps_legacy` on the new typed device, and single `binder_device:chr_file mounton` to allow the pre-existing private-namespace bind-mount target.
- Removed unnecessary `binder_device:chr_file setattr` privilege from the old legacy compatibility policy, so the GNSS launcher can no longer modify mode or ownership of Android's main Binder through that rule.
- `permissive lt013g_gps_legacy;` is **intentionally retained** until outdoor satellite-fix and all chroot child services are verified; this is a policy-preparation milestone, not full-domain Enforcing certification.
- The common policy references the unique T311 Binder device path; a T310/T315 system that does not create this exact inode receives no additional Binder device permissions.

## Checks and result

- Android `m -j3 sepolicy sepolicy_neverallows` job `2223335f293c` EXIT0; after removing the old broad `setattr` rule incremental job `de67d895ebcd` EXIT0.
- Compiled CIL contains exactly `(genfscon binder /lt013g-gps ...)` and the typed GNSS-only allow rule. Built `out/target/product/lt013g/root/sepolicy` SHA256 depends on the final compilation; this file is a build artifact and **not an installed on-device policy**.
- `tools/test_g382_gps_private_binder_policy.py` validates source labels, default platform Binder preservation, typed permissions, no new binderfs filesystem mount permission and the compiled CIL. PASS.
- No kernel/BOOT, ROM SYSTEM, SIM, radio, EFS or GNSS hardware writes in this step.

## Pending hardware acceptance

- Integrate policy in a future coherent signed target-files release, with reproducible binary diff.
- Verify real GNSS NMEA/satellites/outdoor fix, then audit **all** remaining denials with selected domains in Enforcing and only the necessary narrow source rules.
- Separately debug Android `reboot recovery`: current pstore records a successful `cmd=shell` kernel restart reaching Samsung INFORM2/INFORM3 and raw SWRESET, **not** proof that the recovery reboot path works. Do not repeat a failed reboot-to-recovery as a supposed diagnostic fix.

## G382 fully integrated guarded signed OTA, after the source policy commit

- Full Android11 target-files-package job `10c6290329f9` exit code 0, source target-files SHA-256 `88e26cc75e5354c2dab7e0c74490d1f9b6365e3720b1b4e1e46f5fdd0cbd47c2`; target-files validator PASS with physically established G378 BOOT/kernel pin corrected in Git `589b201`.
- Fail-closed staging script `tools/stage_g382_binder_audio_target_files.py` (Git `3334b8d`) verified the **compiled** GNSS Binder genfs and typed allow rules in vendor CIL as well as source audit of BOOT, TWRP RECOVERY, modem RIL and libaudiohal. Staged target-files SHA-256 `c394839ad95b14de6334de15d48dfa651cfb7b5de431bc80fc12899ad8abf795`; compiled vendor_sepolicy.cil SHA-256 `0f66ed0a59659f77a606cb3c93a6cf0831e3d69a8955b212613c6cfe29fd8272`.
- Full standard AOSP OTA generator job `3373ff01263e` EXIT0 and G379-derived strict TWRP pre/post-BOOT guard signed OTA job `e5c9d2214b9f` EXIT0.
- **Final G382 guarded signed OTA** under `/home/debian/Projekty/samsung/artifacts/SM-T311-G382-PRIVATE-GPS-BINDER-20261016/SM-T311-G382-GUARDED-AUDIO-GPS-TWRP-SIGNED.zip`, 484,326,939 bytes, SHA-256 `1c969cae57b7663f0eaba8360b76c59f1ab3f46c7d49f601ab0405ab23bce34b`.
- Independent exact OTA sparse SYSTEM verification job `c3214b58afc5` EXIT0: 274,149 new + 6,828 zero blocks, **1,122,914,304 bytes compared**, no writes to RECOVERY. BOOT SHA remains `cad5e76faf372beff3243ec615924286096713c994a6e5a03d3231f69478e6fd`. New AUDIO remains SHA `9debeb1023a86cfa9aa54775f9d3a12d0c9c0ac2c62c037852306811b54b8a59`; G368 RIL remains exact.
- Diff against G381 target-files: zero added/deleted entries, six changed: `ROOT/sepolicy`, `SYSTEM/vendor/etc/selinux/vendor_sepolicy.cil`, `SYSTEM/vendor/etc/selinux/precompiled_sepolicy` and regenerated `IMAGES/system.img`, `cache.img`, `userdata.img`. `cache.img` and `userdata.img` are not flashed by the guarded OTA. Everything else is byte-identical by ZIP CRC+size including BOOT, RECOVERY, audio and RIL.
- **Physical staging**: original G381 staging ZIP removed from `/data/local/tmp` after verifying expected SHA; host artifact retained. G382 exact signed ZIP pushed to `/data/local/tmp/SM-T311-G382-GUARDED-AUDIO-GPS-TWRP-SIGNED.zip` and read-back SHA `1c969cae57b7663f0eaba8360b76c59f1ab3f46c7d49f601ab0405ab23bce34b` matched. Physical tablet remains **G379** boot_completed=1 / SELinux Enforcing / SIM LOADED UMTS / audio policy found. **G382 has NOT BEEN INSTALLED**.
- Reboot-to-recovery remains a separate investigated fault. The previously preserved pstore shutdown trace with `cmd=shell` shows Samsung INFORM2/INFORM3 and USB/CP teardown reaching the final restart handler; it does not demonstrate that the Android `reboot recovery` path is working. No software reboot-to-recovery was attempted while staging G382.
- Public GitHub push is still blocked by absent authorized Samsung repo remote; only locally committed source is available. Samsung proprietary binaries are not appropriate to push to public source repositories.
