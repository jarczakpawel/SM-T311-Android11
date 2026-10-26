# SM-T311 G381 - integrated Audio HAL 6.0 signed OTA

Date: 2026-10-16

## Built and verified offline

- Android 11 `lineage_lt013g-userdebug`, Linux 6.12.112 source-built G378 BOOT retained.
- Target-files build `cf29bc0ba5bd` completed exit 0. Full OTA generator `7683c8fd01cb` completed exit 0. Guard signing `1ca5c4cfe2f9` completed exit 0. Byte audit `118dec5fe923` completed exit 0.
- Source target-files SHA-256 `cff7313fe7627d50c2d710f3005efdcbfd0d049c72771369d576d468386fddc1`, size 1,149,690,204 bytes.
- Canonical staged target-files with exact OEM recovery fstab SHA-256 `8f517960dc0bd36801f308e5c5ebac4f0c2c85dc2393a0e429a15002d05f6b78`, size 1,149,692,244 bytes.
- Guarded signed final OTA SHA-256 `ddc0bf4c3f8e9e419ce2ce134b9ef613ee0f598c30af00439903292c0d7b2596`, size 484,324,776 bytes.
- OTA/system image audit: 274,149 new + 6,828 zero blocks, 1,122,914,304 data bytes compared, recovery not written. Stock OEM RIL untouched.
- BOOT SHA `cad5e76faf372beff3243ec615924286096713c994a6e5a03d3231f69478e6fd`, RECOVERY SHA `f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f`, G368 RIL SHA `719750b0f544238c0986ecbbc8c7d4b59b822f00c7ba1c883809b1b54fcee491` all pinned.
- ARM32 corrected `libaudiohal.so` SHA `9debeb1023a86cfa9aa54775f9d3a12d0c9c0ac2c62c037852306811b54b8a59`.
- Both `android.hardware.audio@6.0::IDevicesFactory/default` and `android.hardware.audio.effect@6.0::IEffectsFactory/default` included in merged vendor VINTF manifest.

## Physical state

- G379 remains installed with its live VINTF repair, Android boot_completed 1, global SELinux Enforcing, Plus SIM LOADED and data connected.
- New G381 signed guarded OTA is **staged but not installed** at `/data/local/tmp/SM-T311-G381-GUARDED-AUDIO6-TWRP-SIGNED.zip` with identical device SHA `ddc0bf4c3f8e9e419ce2ce134b9ef613ee0f598c30af00439903292c0d7b2596`.
- Removed two superseded OTA staging files from Android `/data/local/tmp`, freeing ~924 MiB; host copies unchanged.
- No Android->recovery reboot attempt: previous G379 software reboot-to-recovery failed repeatedly. Do not repeat a failed reboot simply to advance the update; recovery install can be done when TWRP is physically available.
- OEM firmware/proprietary blobs remain excluded from public source distribution; GitHub remote for Samsung repos is not connected. Public release pending legal/owner-controlled provisioning and functional acceptance.

## Source commits

- `c3eb194` - SM-T311-only VINTF audio 6.0
- `c0b4aaebf6` - corrected HAL version sentinel, frameworks/av
- `4554b3f` - OTA audio guard and G379 postflight
- `e964005` - G381 safe target-files staging and checks

The package uses a development testkey, not a production/private release signing key. It does not change modem EFS, RECOVERY or userdata.

## Diff against released G379 target-files

Compared all 3,804 non-directory ZIP entries by CRC32 and uncompressed size against the exact signed G379 source target-files with the same recovery fstab. There are zero added or removed entries and exactly five changed:

- `SYSTEM/lib/libaudiohal.so` - intended corrected source build.
- `SYSTEM/vendor/etc/vintf/manifest.xml` - intended HIDL Audio 6.0 declaration.
- `IMAGES/system.img` - rebuilt image containing these SYSTEM changes.
- `IMAGES/cache.img` and `IMAGES/userdata.img` - regenerated build artifacts; the guarded OTA does not write these partitions.

No other direct `SYSTEM/` file changed. The full block-by-block OTA/system parity gate passed separately, so the ZIP metadata comparison is not being used as a substitute for byte verification.
