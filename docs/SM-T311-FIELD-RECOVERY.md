# SM-T311: clean-install Wi-Fi/GPS, RADIO and XMM6262 diagnosis

Applies only to the **SM-T311 (lt013g)**. The v1 Android 11 TWRP OTA replaces **SYSTEM and BOOT**. It does not provision USERDATA or replace RADIO, EFS, m9kefs or the bootloader.

## Wi-Fi: missing configuration on an empty USERDATA partition

Android 11 HIDL `wpa_supplicant` expects `/data/vendor/wifi/wpa/wpa_supplicant.conf`. If that file is absent, its interface setup can fail with `Conf file does not exists` even when the BCM4334 is correctly detected.

The device source provides a 266-byte, non-personal template at `android11/device/samsung/lt01-common/wifi/wpa_supplicant.conf`, installed to `/vendor/etc/wifi/wpa_supplicant.conf` by `common.mk`. The HIDL supplicant's `ensureConfigFileExists()` attempts to copy this vendor template when the runtime configuration is missing. The v1 release predates this change; merely checking out the new source does not alter an installed SYSTEM. Android 11 implementation reference: https://android.googlesource.com/platform/external/wpa_supplicant_8/+/3ce898c8242b5a4d7b915c1cf3a479b9b6b1058b/wpa_supplicant/hidl/1.4/supplicant.cpp

On the original v1 image, the following manual recovery initializes **only a missing file**, without copying another device's saved networks:

```sh
adb root
adb wait-for-device
adb push android11/device/samsung/lt01-common/wifi/wpa_supplicant.conf /data/local/tmp/wpa_supplicant.conf
adb shell 'mkdir -p /data/vendor/wifi/wpa/sockets; chown wifi:wifi /data/vendor/wifi /data/vendor/wifi/wpa /data/vendor/wifi/wpa/sockets; chmod 0770 /data/vendor/wifi /data/vendor/wifi/wpa /data/vendor/wifi/wpa/sockets'
adb shell 'if [ ! -e /data/vendor/wifi/wpa/wpa_supplicant.conf ]; then cp /data/local/tmp/wpa_supplicant.conf /data/vendor/wifi/wpa/wpa_supplicant.conf; fi; chown wifi:wifi /data/vendor/wifi/wpa/wpa_supplicant.conf; chmod 0660 /data/vendor/wifi/wpa/wpa_supplicant.conf; restorecon -RF /data/vendor/wifi; sync'
adb shell svc wifi enable
```

The final `chown` and `chmod` require that the file exists; stop and inspect any errors. The template includes no MAC addresses, SSIDs, passwords or radio calibration.

## GPS/GNSS

The legacy BCM4752 runtime is already provided by the installed vendor payload `/vendor/etc/lt013g-gps-legacy/rootfs-mutable.tar` and service `/vendor/etc/init/init.lt013g.gps-legacy.rc`. Its work directory is `/data/vendor/gps/legacy-v195`. Runtime logs, state databases and the sensor-service HMAC key are not portable and must not be distributed. Copying an entire donor `/data/vendor` is **not** the default repair.

Check the service and GNSS HAL, then obtain an actual position fix outdoors before claiming GPS has been verified:

```sh
adb shell 'ls -l /vendor/etc/lt013g-gps-legacy/rootfs-mutable.tar; getprop init.svc.vendor.lt013g-gps-legacy-v195'
adb shell dumpsys location
```

## RADIO image: compatibility and recovery

The known-working reference uses bootloader `T311XXSBQB2` and baseband `T311XXUBNI1`:

| Reference | Value |
| --- | --- |
| RADIO block node in Android | `/dev/block/by-name/RADIO` -> `/dev/block/mmcblk0p13` |
| RADIO block node in TWRP 3.1.1 | `/dev/block/platform/dw_mmc/by-name/RADIO` -> `/dev/block/mmcblk0p13` |
| Size | `20971520` bytes |
| Reference SHA-256 | `e0fc3aa24bb85daf34517f1072df84ab8ecf27a2f807bf8c61b9974156578b9c` |
| Original known-working BOOT SHA-256 | `acc2a0579e0f4975da3b569941534f5b572ad9a7ae2bf4409fd88b5f633dd565` |

A different RADIO hash alone does **not** prove it is faulty. Flashing a raw RADIO image replaces the **entire** partition, including any modem NV content stored there. EFS and m9kefs are separate and must be preserved. Do not clone device-specific EFS, IMEI, m9kefs, SIM keys or Wi-Fi/Bluetooth calibration. Proprietary RADIO binaries are intentionally not stored in public Git; obtain firmware from an authorized source and retain a device-specific backup.

If an experienced user elects to compare RADIO images, first verify model, path and size in TWRP, then make a backup on the computer:

```sh
adb reboot recovery
adb wait-for-recovery
adb shell readlink -f /dev/block/platform/dw_mmc/by-name/RADIO
adb shell blockdev --getsize64 /dev/block/platform/dw_mmc/by-name/RADIO
adb shell 'dd if=/dev/block/platform/dw_mmc/by-name/RADIO of=/tmp/RADIO-original.img bs=1048576'
adb shell sha256sum /dev/block/platform/dw_mmc/by-name/RADIO
adb pull /tmp/RADIO-original.img
wc -c RADIO-original.img
sha256sum RADIO-original.img
```

**Stop** unless the block node resolves to `mmcblk0p13`, both sizes are exactly 20 MiB and the backup checksum matches the original partition. A vetted, raw, 20 MiB RADIO image is written from TWRP only after checking its SHA-256 and saving the backup. **Do not** use `dd` on the Android-sparse `system.img`; it requires the OTA's sparse-aware installer. If RADIO replacement does not restore modem enumeration, restore the device's own backup rather than replacing EFS/NV from a donor.

## XMM6262 test kernel and log collection

An external SM-T311 had Wi-Fi connectivity after its missing USERDATA configuration was restored, but the modem did not enumerate on HSIC even with matching BOOT, CBD/RIL and RADIO hashes. This isolates the observed failure before RIL registration; it does not establish a hardware defect.

The diagnostic kernel changes CP_ON readback only. Samsung's XMM6262 power-on sequence uses `CP_ON=1`, `udelay(60)`, `CP_ON=0`. We retain this **60 microsecond delay after confirmed HIGH**, with bounded (200 microsecond) GPIO readback using the GPIO API. Unlike raw `GPL2_DAT` reads, the GPIO API handles the Exynos controller's peripheral clock. `CP_ON_GPIO_SYNC` logs the observed readback latency. This change is **experimental** and not a confirmed fix on the second unit.

Collect logs after a fully powered-off start (USB disconnected during shutdown and boot, allow at least 30 seconds before powering on):

```sh
adb root
adb wait-for-device
adb shell cat /sys/devices/platform/xmm6262-modem-control/diag_snapshots > modem-gpio.txt
adb shell cat /sys/devices/platform/xmm6262-modem-control/state > modem-state.txt
adb shell cat /sys/devices/platform/xmm6262-modem-control/link_trace > modem-link.txt
adb shell cat /proc/tab3_xmm6262_handoff_trace > modem-handoff.txt
adb shell dmesg > dmesg.txt
adb logcat -d -b all -v threadtime > logcat.txt
adb shell 'for d in /sys/bus/usb/devices/*; do [ -f "$d/idVendor" ] && echo "$d $(cat "$d/idVendor"):$(cat "$d/idProduct")"; done' > usb.txt
```

Look for HSIC BOOTROM `058b:0041`, runtime `1519:0020` and `ril.cbd.boot_done=1`. Logs may contain device identifiers. Do not publish them without review.


## BOOT-only test installation (no Android rebuild)

The experimental XMM6262 CP_ON GPIO readback kernel was rebuilt from the Linux 7.3.0 source and packed with the **unchanged ramdisk** from the published working BOOT image. No SYSTEM, RADIO, USERDATA or EFS update is required to try it.

- Reference BOOT SHA-256: `acc2a0579e0f4975da3b569941534f5b572ad9a7ae2bf4409fd88b5f633dd565`
- Experimental BOOT SHA-256: `09f43deb8f04aaa9a4a3546203d835611a1100df0fb3b2e1933f224b8e9bb307`
- BOOT node in TWRP 3.1.1: `/dev/block/platform/dw_mmc/by-name/BOOT` -> `/dev/block/mmcblk0p9`
- BOOT size: `8388608` bytes

In TWRP, confirm the BOOT block path, size and original checksum before writing. Back up the original BOOT to the computer and confirm its SHA-256. Then upload the 8 MiB experimental image to `/tmp` and use `dd` against the **verified** BOOT block node. Read back the entire block and verify its checksum before rebooting. To leave this TWRP version for Android, use `adb shell twrp reboot`, not plain `adb reboot` (the latter may leave the recovery boot flag set). For the complete test procedure, see the README shipped with the experimental BOOT archive.

On one working SM-T311, the experimental BOOT reached Android `sys.boot_completed=1`, XMM6262 `ONLINE`, `ril.cbd.boot_done=1`, runtime HSIC `1519:0020` and connected Wi-Fi. It measured `CP_ON_GPIO_SYNC rise_ns=4375 fall_ns=2459 pulse_us=60`. This **does not** demonstrate that the change resolves the other tablet's failure.
