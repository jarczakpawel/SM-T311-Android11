# SM-T311 3G: Android 11 / Linux 7.3.0-pjarczak G1466-G1471 source and physical-config contract

**Production BOOT stays on the physically accepted G1441 / G958 image. The source-only work described here has not been flashed.**

## G1492 bit-identical real G1130 kernel source rebuild - supersedes G1484 and G1476 hold

The final zImage mismatch was caused by the temporary **weak** kernel UTS string, not by any Mali/Exynos/MFC code difference. Linux7.3 uses `init/version.o` with a placeholder `init/utsversion-tmp.h` in its normal object list, then links the **strong** `init/version-timestamp.o` generated at the final build stage. Earlier attempts applied `KBUILD_BUILD_TIMESTAMP` to both stages, adding a full date to the temporary weak object. That unnecessary 32-byte change shifts kernel symbols and KALLSYMS addresses, changing the entire compressed image. Original G1130 uses the placeholder `# SMP PREEMPT ` for the temporary weak object, while final strong `UTS_VERSION` is `#1 SMP PREEMPT Sat Oct 24 16:54:34 CEST 2026`.

G1492 fixes two target-local Kbuild variables on `init/utsversion-tmp.h`, setting `build-version` and `build-timestamp` to the empty placeholder independently of final UTS. The final strong header, running version string, actual drivers and physical 5,156-symbol Kconfig stay unchanged. Using the accepted G1130 source merge, the original 16:49:41 CEST initramfs stamp (512-byte CPIO SHA `f0feae07c6dfcb6c2362c921dc8ddafbf85d902b627afee50be46d39365c1cc6`), the original final UTS 16:54:34 CEST, the original GCC16.2/binutils2.47 and exact physical `sm_t311_android11_defconfig`, G1492 host kernel source build yields:

- `init/version.o` **bdfe382f6520aa0f709ca8bf8d97c9fb31b616ff2c6dc581735e5e1cbcaf7816** - original weak object exact
- `init/version-timestamp.o` **a6368b765090ded1080888e0b24a7ec0a534dd343b8f999572b1089731452348** - original strong final timestamp exact
- `vmlinux` **63c180b446107824e5b8617a07af9cd7ae4d9e3a6e5ac1dca922c488dada4b0c** - original full linked ELF byte exact
- `arch/arm/boot/zImage` **4f6d00ea7230ad6a0a43311bb09ace7451b430698d126eb059279e11feedbea6** - real accepted physical G1130 boot kernel byte exact
- `exynos4212-tab3-3g8.dtb` **6fb3e77075733d531abaa2f4c3fa1bf55c11a50f04cc5acc9838db5ea0f41bfb** - real accepted hardware DTB exact

All five hashes were verified against archived G1130 originals, not fabricated. `scripts/smt311/g1486-build-source-g1130-hostonly.sh` is updated to guard four generated SHA and refuse mismatched release; it never packages or flashes a BOOT image. Existing older notes below describe *superseded failed rebuild attempts*, not the accepted current source status. Keep the physically accepted G1441 BOOT on the tablet unchanged. Linux docs on reproducible builds: https://www.kernel.org/doc/html/latest/kbuild/reproducible-builds.html .

## G1484 physical BOOT source restored to the publishing kernel branch

The active physical BOOT in accepted Android SMT311G1441 is **G1130**, not G958. A G958-only rebuild cannot be bit-identical and, more importantly, silently **loses the working hardware-video MFC recording devfreq PM-QoS fix**. The exact accepted G1130 source is commit `9090d2fa76cb3128542be2a2b3f9bf18ba8288cb`, with parent `ea732082` (read-only encoder activity) and G958 ancestor `0d7c924`. Previously it existed only in a separate `sm-t311-publish-g1130-verified` Git branch; the default publishing branch was still G958.

**G1478 Git `bfcbf6cd89a6c98954bea90e7fc84125f65780be` explicitly merged accepted G1130** into default `sm-t311-active-g958` with `--no-ff`, preserving all subsequent production-defconfig / integrity-guard commits. The merge changes exactly three driver files `drivers/media/platform/samsung/s5p-mfc/{s5p_mfc.c,s5p_mfc_common.h,s5p_mfc_enc.c}`. Their file SHA256 hashes are exactly the blobs at accepted G1130. Original HZ200, OTG, DEBUG_FS, GPIO/USB/modem/GPS, camera/Codec2 and Mesa source are untouched. `scripts/smt311/verify_g1481_accepted_g1130_source.py` fails closed if G1130 is not an ancestor or any of those three files, full physical config or 3G DTB differs; it can verify current HZ200 and isolated HZ300 host build outputs as independent options. The G1441 physically accepted release tags are intentionally immutable; G1478+ are post-release source provenance fixes.

**Rebuild evidence:** ARM32 200Hz HZ G1130 source recompiled to `7.3.0-pjarczak` with exact `KBUILD_BUILD_TIMESTAMP=Sat Oct 24 16:54:34 CEST 2026`, `KBUILD_BUILD_VERSION=1`, `KBUILD_BUILD_USER=debian`, `KBUILD_BUILD_HOST=debian`. The archived original G1130 empty initramfs is independently source-reproducible only with earlier timestamp `Sat Oct 24 16:49:41 CEST 2026` (Unix `1792853381`). Its 512-byte newc CPIO SHA is `f0feae07c6dfcb6c2362c921dc8ddafbf85d902b627afee50be46d39365c1cc6`. Generated from `usr/gen_initramfs.sh`, `usr/default_cpio_list` and host `usr/gen_init_cpio`, not copied blindly from a private blob. The difference between initramfs time and final UTS is exactly **293 seconds**, confirming the archival build steps were not a single timestamp injection. After regenerating the exact CPIO and relinking, **2538 of 2548** original `*.o` files (including MFC, sched, DRM, initramfs) match archived G1130 bit-for-bit; ten remaining differences include host tool objects, the old archive's stale `init/version.o`, and final compressed `piggy.o`/`vmlinux.o`. Production `zImage` now SHA `5a5595d248cd16a000594ced1447111c3017c5c576e714a7396ba57b0b63fb35` (6,386,984 B) still differs from accepted `4f6d00ea7230ad6a0a43311bb09ace7451b430698d126eb059279e11feedbea6` (6,387,040 B). **NO BIT-EXACT FULL KERNEL CLAIM.** The standalone accepted original compiled `zImage` physically delivered under original G1130 private build has exactly the accepted SHA, and its corresponding source commit is now merged. Never use the rebuilt different SHA as an allegedly identical replacement.

The HZ300 isolated experiment has also been **rebuilt after G1478**, now including the actual physical G1130 encoder PM-QoS source. Host ARM32 `zImage+dtbs` with `CONFIG_HZ=300`, preserved T311 MAX77693 experimental OTG, `7.3.0-pjarczak`, matching accepted exact 3G DTB, and embedded `G1130_MFC_RECORD_QOS` string PASS; new candidate SHA `603802435bfb30e4ec4e381f62210433f69c652e8ab4f06c117b5cb241e70254`. HZ300 remains HOST ONLY, never flashed or validated on real hardware. Do not confuse pre-G1478 G958-only HZ300 candidate output with this correct post-merge source.

## Source reproducibility fix

The former `arch/arm/configs/sm_t311_android11_defconfig` dated to a Linux 6.12.98 build and did **not** regenerate the accepted current kernel configuration; critically, its `CONFIG_DEBUG_FS` and `CONFIG_CHARGER_MAX77693_T311_OTG_EXPERIMENTAL` were unset relative to the physical G1441 image. This would drop accepted Exynos/USB OTG behavior if a future developer rebuilt from this source blindly.

The physical Android11 G1441 device reported its running `/proc/config.gz`; the generated config `arch/arm/configs/sm_t311_android11_g1441_physical_defconfig` has SHA256 **ca7d1c72025b012c34dca79ff65b234ab7e0b6b154bf04f7aaa4c06b0752c988**. It has **5156** fully known Kconfig symbols. Re-running the kernel's `sm_t311_android11_g1441_physical_defconfig` target regenerated *all 5156 symbols with zero differences*. The default production `sm_t311_android11_defconfig` now equals this physical file byte for byte. It preserves `CONFIG_HZ=200`, `CONFIG_PREEMPT=y`, `CONFIG_DEBUG_FS=y`, OTG experimental support, 7.3.0-pjarczak localversion, and all other accepted subsystem flags.

`scripts/smt311/verify_g1467_physical_kernel_config.py` enforces the physical full-file SHA, all 5156 Kconfig symbols, byte-exact production defconfig and comparison to regenerated build output.

## Distinct host-only HZ300 scheduling candidate

`arch/arm/configs/sm_t311_android11_g1441_hz300_experiment_defconfig` is based on the **actual** accepted G1441 physical full config, not the older 6.12-era file. Its Kconfig diff is exactly the three settings `CONFIG_HZ_200=y` -> disabled; `CONFIG_HZ_300` disabled -> enabled; `CONFIG_HZ=200` -> `300`. The validation script checks no unrelated flags changed, particularly MAX77693 T311 OTG, DEBUG_FS, PREEMPT and the kernel release. The incorrect preliminary G1465 candidate based on the stale defconfig is removed from active source, preserved only in Git history.

Why consider it: Android AOSP recommends CONFIG_HZ=300 for some interactive ARM devices to reduce scheduling jitter, but this is a hypothesis on SM-T311 without pre/post traces. Effects on jiffies-based legacy drivers must be considered; do not enable `sys.use_fifo_ui` or arbitrarily increase CPU/GPU clocks.

Primary AOSP reference: https://source.android.com/docs/core/tests/debug/jank_jitter

## Bitwise verification of the accepted BOOT

Accepted full BOOT image (already part of signed G1441 OTA): SHA256 **acc2a0579e0f4975da3b569941534f5b572ad9a7ae2bf4409fd88b5f633dd565**, 8388608 bytes, AOSP BOOT page size 2048. Kernel payload contains a standalone ARM32 compressed zImage **6,387,040 bytes** SHA256 **4f6d00ea7230ad6a0a43311bb09ace7451b430698d126eb059279e11feedbea6**, followed by DTB **86,489 bytes** SHA256 **6fb3e77075733d531abaa2f4c3fa1bf55c11a50f04cc5acc9838db5ea0f41bfb**, exactly the DTS `samsung/exynos4212-tab3-3g8.dtb` for **SM-T311 3G**, not the `lte8` (SM-T315) device.

A fresh 200-Hz host kernel build from the corrected G1441 config successfully produced ARM32 `zImage` and `dtbs`, `UTS_RELEASE=7.3.0-pjarczak`. Its `exynos4212-tab3-3g8.dtb` was byte-identical to the accepted physical BOOT DTB. **Its zImage is NOT byte-identical:** newly generated SHA256 `e945daab1d90e24bdafe578497d365d0426f0b53234dd8ace89befb785169ff2`, 6,386,704 bytes, vs accepted standalone zImage 6,387,040 bytes. Exact reason for compressed binary difference remains unverified. Matching Kconfig + DTB + UTS_RELEASE proves source configuration and DTB compatibility, not a bit-for-bit ROM reproduction or physical boot correctness.

`scripts/smt311/verify_g1468_physical_zimage.py` verifies signed G1441 OTA boot SHA, BOOT layout, 3G DTB bytes (separately), extracts the standalone zImage by stripping the known 3G DTB, and **refuses success** for a mismatching rebuilt zImage. To validate only the accepted 3G DTB, use `--dtb-only`.

Reference host commands, from the complete public Android+kernel source checkout with matching `arm-linux-gnueabihf-` toolchain:

```bash
make -C kernel O=/path/to/build/G1441-exact ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- sm_t311_android11_defconfig
python3 kernel/scripts/smt311/verify_g1467_physical_kernel_config.py --generated-production /path/to/build/G1441-exact/.config
make -C kernel O=/path/to/build/G1441-exact ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- LOCALVERSION= -j4 zImage dtbs
python3 kernel/scripts/smt311/verify_g1468_physical_zimage.py --accepted-ota /path/to/SM-T311-G1443-G1441-TWRP-OTA.zip --built-zimage /path/to/build/G1441-exact/arch/arm/boot/zImage --built-3g-dtb /path/to/build/G1441-exact/arch/arm/boot/dts/samsung/exynos4212-tab3-3g8.dtb --dtb-only
```

The last command with `--dtb-only` verifies only the DTB, NOT that the newly built zImage is safe to flash. Omit that flag to enforce the full zImage equality check.

## Flash safety

Do **not** flash G1465-G1471 kernels to the physical tablet. No OTA or TWRP install was performed. Accepted modem RIL, GNSS, recording/camera, Codec2, USB, Mesa, audio and SELinux state must remain intact. Don't create backups or push to GitHub; project owner publishes only after source and hardware review.
