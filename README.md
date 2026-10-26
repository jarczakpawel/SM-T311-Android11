# Samsung Galaxy Tab 3 8.0 — SM-T311

**Android 11 (LineageOS 18.1) · Linux 7.3.0 · Mesa 26.2.5**

## Device

| Specification | Details |
|---|---|
| Model | **SM-T311 / lt013g (3G)** |
| Processor | Samsung Exynos 4212, ARM Cortex-A9 |
| GPU | Mali-400 MP4, Mesa/Lima |
| RAM / display | 1.5 GB / 1280 × 800 |
| System / kernel | Android 11 / 7.3.0 |
| Release | G1786, `userdebug`, AOSP test keys |

## Hardware

| Component / feature | Status |
|---|---|
| Display, backlight, touchscreen | Works |
| Mali-400, EGL, GPU acceleration, HWComposer | Works |
| Wi-Fi | Works |
| Bluetooth | Works |
| eMMC storage / microSD card | Works |
| USB: ADB, MTP, OTG | Works |
| ADB over Wi-Fi (port 5555) | Works |
| GPS / A-GPS — Broadcom BCM4752 | Works |
| XMM6262 modem, SIM, voice calls, SMS | Works |
| EDGE/3G mobile data | Works |
| Camera: photos, preview, video | Works |
| Video decoding and playback | Works |
| FIMC, hardware JPEG and MFC codec | Works |
| Speaker, microphone, system sounds | Works |
| Vibration / Vibrator HAL | Works |
| Screen recording | Works |
| LIS2DH12 accelerometer | Works |
| YAS532 magnetometer / compass | Works |
| CM36653 ambient light sensor | Works |
| CM36653 proximity sensor | Works |
| Hall sensor / magnetic cover | Works |
| Infrared (IR) | Works (driver and HAL) |
| MHL / video output | Tested |
| Battery, charging and RTC | Works |
| Power, Home and volume buttons | Works |
| TWRP / system installation | Works |
| SELinux | Enforcing |

## Changes

| Area | Work completed |
|---|---|
| Linux 7.3.0 kernel | ARM32/Exynos 4212 port, Device Tree, drivers, interrupts, RTC |
| Memory | Full 1.5 GB (PFN holes/HIGHMEM), CMA, DMA-BUF, zRAM/LZ4, PSI and `lmkd` fixes |
| Display / touch | FIMD/DSI, LP8556 backlight, MMS114, VSync, screen rotation |
| Graphics | Mali-400, Mesa 26.2.5/Lima, DRM/G2D, minigbm, HWC, EGL, SurfaceFlinger, Skia |
| Wi-Fi / Bluetooth | BCM4334/SDIO, Linux 7.3 Netlink compatibility, Android services |
| Modem | XMM6262: HSIC, RIL, SIM/PIN/PUK, EDGE/3G, PDP, calls, SMS, watchdog |
| GPS | BCM4752: GNSS/NMEA, A-GPS, SUPL, LTO/XTRA, time and cellular assistance |
| Camera / photos | Camera2, FIMC, V4L2, hardware JPEG, NV12, DMA-BUF |
| Video | MFC, Codec2, hardware H.264 encoding, decoding and playback |
| Audio | WM1811, Audio HAL, microphone, ringtones, headset detection |
| Screen recording | Screenrecord, aspect ratio and orientation fixes |
| Files / FUSE | MediaProvider, FUSE 7.41, inode protection, permissions, safe fallback |
| Storage / USB | eMMC, microSD, exFAT, vold, ADB, MTP/PTP, OTG, FunctionFS |
| Power | MAX77693, S5M8767, MAX17050, VBUS, charging, DVFS, thermal management |
| Sensors | LIS2DH12, YAS532, CM36653, Hall, IR, IIO/HAL integration |
| Apps | Gallery2 (video seeking), Camera2, developer settings |
| Interface | SystemUI, location tile, Quick Settings, Trebuchet |
| Security | SELinux, HAL policies, MediaProvider/APEX, seccomp, SIM protection |
| Cleanup | Removed Traceur, Seedvault, DSU, Trust and unnecessary debugging tools |
| Release | Android and kernel sources, reproducible Git patches, TWRP images with SHA256 |

## Source code and images

| Directory | Contents |
|---|---|
| [`kernel/`](kernel/) | Complete Linux 7.3.0 kernel sources |
| [`android11/`](android11/) | 863-project manifest, 34 patches, two device trees |
| [`android11/scripts/`](android11/scripts/) | Source reconstruction and build scripts |
| [`images/`](images/) | TWRP installer, images and SHA256 checksums |
| [`docs/`](docs/) | Build instructions |

**Installation:** Download `SM-T311-G1788-G1786-FINAL-CLEAN-ANDROID11-TWRP-OTA.zip` from [GitHub Releases](https://github.com/jarczakpawel/SM-T311-Android11/releases/tag/v1) and install it using TWRP. **SM-T311 only.** The `system.img` file uses Android sparse format; do not write it directly with `dd`. Checksums: [`images/SHA256SUMS.txt`](images/SHA256SUMS.txt).

**Building:** See [`docs/BUILD.md`](docs/BUILD.md). The Android patches reproduce the final Git source trees. Compatible proprietary vendor files and locally built ARM32 Mesa libraries are required.

**Note:** Unofficial community build, **not an official Samsung or LineageOS release**. Built as `userdebug` and signed with AOSP test keys. Kernel: GPL-2.0; other components retain their respective licenses. Redistribution rights for proprietary libraries and firmware must be reviewed separately.
