# SM-T311 Android 11 - firmware baseline and first boot

The v1 TWRP OTA installs SYSTEM and BOOT only. It does not replace
RADIO, bootloader, USERDATA, EFS, or m9kefs partitions.

## Verified working tablet baseline

- Bootloader: T311XXSBQB2
- CP firmware/baseband: T311XXUBNI1
- RADIO: /dev/block/mmcblk0p13 (20 MiB), SHA-256 e0fc3aa24bb85daf34517f1072df84ab8ecf27a2f807bf8c61b9974156578b9c
- BOOT: SHA-256 acc2a0579e0f4975da3b569941534f5b572ad9a7ae2bf4409fd88b5f633dd565
- CBD: SHA-256 03a5758257050aa8ccf3b3e3e46b2f8b054acb4bdb968220dc0af8f8e8adaca4

These are known-working identifiers, not proven minimum requirements.
Updating the bootloader alone did not resolve XMM6262 HSIC enumeration
on another unit. For original firmware information, see:
https://samfw.com/firmware/SM-T311/XEO/T311XXSBQB2

The official XEO firmware pairs T311XXSBQB2 with CP T311XXUBNI1.
Samsung firmware and proprietary binaries are not included in this source
patch because redistribution permission has not been established.

## Wi-Fi after a clean installation

The Android 11 HIDL wpa_supplicant requires
/data/vendor/wifi/wpa/wpa_supplicant.conf. An installation without
an existing /data may lack the configuration, making STA initialization
fail with 'Conf file does not exists'. The source now installs the same
266-byte non-personal template used by a verified working device to
/vendor/etc/wifi/wpa_supplicant.conf. HIDL can use it to create the
missing runtime configuration; existing settings are preserved.

The template contains no SSIDs, passwords, MAC addresses or calibration.

## Device-specific data and GPS

Never clone another tablet's EFS, m9kefs1/2/3, IMEI, SIM state or
Wi-Fi/Bluetooth calibration. These require per-device preservation.

Legacy GNSS uses the vendor-packaged
/vendor/etc/lt013g-gps-legacy/rootfs-mutable.tar, initialized by
/vendor/etc/init/init.lt013g.gps-legacy.rc. Its generated
/data/vendor/gps/legacy-v195 tree need not be copied between tablets.

## Verification

    adb root
    adb shell 'getprop ro.boot.bootloader; getprop gsm.version.baseband'
    adb shell sha256sum /dev/block/by-name/BOOT /dev/block/by-name/RADIO
    adb shell ls -lZ /data/vendor/wifi/wpa/wpa_supplicant.conf
    adb shell cat /sys/class/misc/link_pm/device/state

The absence of the CP at HSIC/BOOTROM enumeration occurs before
RIL registration, and is not by itself evidence of an SELinux problem.
A different RADIO partition SHA alone does not establish which
internal firmware section differs.
