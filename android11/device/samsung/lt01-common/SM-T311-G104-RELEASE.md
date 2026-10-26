# SM-T311 Android 11 / kernel G104 source-to-device release

**Status: physically booted and tested on the SM-T311, 2026-10-13.** This file records what is deployed and what still requires hardware validation. It is for the user's eventual GitHub publication; no remote push has been performed.

## Exactly deployed

| Component | Source/build reference | Physical status |
|---|---|---|
| BOOT p9, Linux 6.12.112-PJARCZAK+ | Kernel local Git branch `sm-t311-g104-release`, commit `d3374f900df52b762631e98d136782fc915448c6` | Physically flashed and 8MiB SHA checked in TWRP |
| BOOT p9 SHA256 | `25d3ada45242849dae3905910e2dbc66647e54c73ff09ea3fe796fb5048ac5d9` | Exact TWRP readback |
| Android kernel payload zImage+DTB | `device/samsung/lt01-common/prebuilt/pjarczak/kernel`; SHA `1233bccf2eff97f843eb21295ac5ab3007705a539d68b0a1fe17b183794f123e` | Synchronised from running G104 |
| Board DTB | SHA `fc68e1fc0902fdcd08a9ebccbc0ba7440e21cb8672e384ac1a17141beed8f993` | Unchanged from G89; 86419 bytes |
| Kernel build .config | `release/sm-t311-g104/android11-g104.config` in kernel Git branch; SHA `7cc855e7a0a149704f5e05b311dc4a66aa027f4ea6eaeea20ffca1af06fa7e07` | Exact released configuration, ARM32 build PASS |
| Android HWC G96 | `external/drm_hwcomposer` four synchronized files | Physically installed ELF32 ARM, SHA `0a095aeab049e35ed69b3958b1ac3adbb86507e73facd26f625f9d8cd146d74d` |
| TWRP recovery p10 | SHA `f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f` | Unchanged |
| Rollback G89 BOOT | `/cache/SM-T311-G104-KERNEL/BOOT-G89-ROLLBACK.img`; SHA `d249b288c7916dd90840140860e3727390a59af059a48a898d7d70df7ecd4ddb` | Persistent recovery-local backup |
| Rollback original HWC | `/cache/SM-T311-G97-HWC/G97-original-HWC.so`; SHA `a629ed7f3dfbcb6e43a95f89d508c248d6f56a9b2e6f6d21f806794f5d47bfe0` | Persistent verified TWRP backup |

Kernel branch includes upstream Linux 6.12.112 plus 37 project-specific edited driver/source files relative to its upstream base and a release-config snapshot. The previous default checkout of kernel sources was 15 stable commits older; **do not publish that old checkout as if it had built G104**. `TARGET_FORCE_PREBUILT_KERNEL=true` in `BoardConfigCommon.mk` deliberately forces this checked G104 payload for Android builds; source build needs the G104 release Git branch.

## Verified physical functionality after G104

- Android reached `sys.boot_completed=1` with Launcher3/SurfaceFlinger active.
- DSI panel connected/enabled, MELFAS MMS114 touch detected, physical sleep→wake display resumed.
- Same Android HWC G96, VSYNC `17107264 ns` (~58.4547Hz); DRM HWC ValidateDisplay/PresentDisplay errors both zero.
- Lima Mali400 devfreq active (160–440MHz) and GPU cooling driver exists, but no long GPU stress test after G104.
- Wi-Fi enabled, Bluetooth ON, modem firmware identifiable; full throughput, calls/data and SIM scenarios need independent tests.
- CameraService reported two devices, ALSA audio endpoints registered, accelerometer/magnetometer/light sensor providers present; this **does not itself prove each camera, audio route and sensor measurement works**.
- Charging reported via USB and battery status, but exhaustive charger/fuel-gauge/OTG role tests remain outstanding.

## Not deployed / not yet proven

- G90 panel-clock rewrite (72161→72727kHz): **not deployed**. Although physical scanout is 58.4545Hz, `mode.clock` also affects hardware VIDCON0 divider, with potential changes for alternative clock parent rates. HWC G96 corrects reported vsync while retaining stable hardware divider.
- G93 proposed FIMD divider-preservation helper: **not applied** (source modification refused); never claim included.
- G102 FUSE telemetry `libfuse_jni.so`: offline compiled only; MediaProvider native SO is nested in read-only signed APK/APEX, no deployment.
- USB OTG host 5V regulator / role switch, AFTR deep idle, modem HSIC reset: electrically/radio high-risk and not demonstrated safe for arbitrary enabling.
- Full camera capture/preview/photo, 3G voice/data, GPS, IR transmitter, all sensors, hotplug, suspend cycles, speaker/mic/headset, and sustained GPU/video/SD card performance are **not all release-gated yet**.
- Shared-storage FUSE 4MiB write stalls physically observed (~9–11s), cause unresolved; 720p60 media startup drops intermittent. Avoid tuning timers/globals without verified root cause.

## Development consistency and rebuild instructions

- The **kernel source branch** and this Android prebuilt must be published together. Source branch: `sm-t311-g104-release` at commit `d3374f9`; it includes build .config and a device hardware release README.
- Android `external/drm_hwcomposer` incorporates the four files matching physically installed G96: `drm/drmmode.cpp`, `include/drmmode.h`, `drmhwctwo.cpp`, `drm/vsyncworker.cpp`. Previous source copies exist in the project artifacts.
- A direct combined-Ninja rebuild of HWC has a host dependency failure `env: python: No such file or directory` and attempted to rebuild ~994 Soong prerequisites. This is **a host build-environment issue**. The deployed HWC G96 had already passed independent production ARM32 compilation, full shared-library link and Soong strip; don't claim the newly synchronized Android checkout was fully rebuilt until that separate issue is fixed.
- Do not overwrite a working source or BOOT image to tidy its name. Always verify full SHA, preserve recovery and restore paths, and test physical boot / DSI / HWC / touch after updates.
- Do not put private radio identifiers, SIM information, Wi-Fi SSIDs, or tablet serial in public release artifacts.

## Relevant upstream background

Mainline Tab 3 8.0 work: https://lists.infradead.org/pipermail/linux-arm-kernel/2023-July/849678.html

postmarketOS device limitations & OTG caveats: https://wiki.postmarketos.org/wiki/Samsung_Galaxy_Tab_3_8.0_%28samsung-lt01%29

Upstream FIMD clock-divider implementation: https://codebrowser.dev/linux/linux/drivers/gpu/drm/exynos/exynos_drm_fimd.c.html

Upstream Lima scheduler and GPU runtime PM: https://code.googlesource.com/linux/torvalds/linux/+/dab01c597f6bd40e0efe7da967b8374ca1971b79/drivers/gpu/drm/lima/lima_sched.c
