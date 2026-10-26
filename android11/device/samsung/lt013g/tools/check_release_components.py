#!/usr/bin/env python3
import argparse
import hashlib
import io
import sys
import zipfile
from pathlib import Path

EXPECTED_BOOT = '81b01abb5612a9a777330705c02c4bc1f49386da4d5df0950f955246f9fa6bce'
EXPECTED_RECOVERY = 'f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f'

EXPECTED = {
    'lib/egl/libEGL_mesa.so': '01a219dc07c8b5ba752d02f8fed1ea5bedd92fe45292eaa082be8fb5a8ce5611',
    'lib/egl/libGLESv1_CM_mesa.so': '5c8471d06466ba5beb19e3cd2d74fd08fc611ddd34412c5d91a893b6381ca8c0',
    'lib/egl/libGLESv2_mesa.so': '10206f8df94e1e36ede21ba0577503007603d4d6eeb5be1e8e7316604878554f',
    'lib/libgallium_dri.so': '04148b6de255b5c563695a00f1517a788d1923d0ed825e5be18c1f069b20ae76',
    'lib/libdrm_mesa26.so': 'cb86a1cab9f6b807d6f83355964a546592791f64ee3247320a087f7379485881',
    'lib/hw/audio.primary.lt013g.so': '636743a098acb4ab31bad39adb3c46875c461d4b313f5fddb6c2106318224cfa',
    'bin/t311-audio-ril': '79faf768b1af78cca260018988950e2de8f8ec591e9022f5b5f47ae159cb4041',
}

LINUX73_GENFS = {
    '/devices/platform/i2c-10/i2c-10/10-0036/power_supply/max170xx_battery/wakeup12': 'sysfs_wakeup',
    '/devices/platform/i2c-9/i2c-9/9-0066/max77693-charger/power_supply/max77693-charger/wakeup18': 'sysfs_wakeup',
    '/devices/platform/i2c-9/i2c-9/9-0066/wakeup/wakeup8': 'sysfs_wakeup',
    '/devices/platform/i2c-12/i2c-12/12-0060/wakeup/wakeup16': 'sysfs_wakeup',
}
for filename in ('name', 'in_illuminance_raw', 'in_proximity_raw'):
    LINUX73_GENFS[f'/devices/platform/i2c-12/i2c-12/12-0060/iio:device2/{filename}'] = 'sysfs_lt013g_legacy_iio'
for filename in ('name', 'in_magn_x_raw', 'in_magn_y_raw', 'in_magn_z_raw',
                 'in_magn_x_scale', 'in_magn_y_scale', 'in_magn_z_scale'):
    LINUX73_GENFS[f'/devices/platform/i2c-11/i2c-11/11-002e/iio:device3/{filename}'] = 'sysfs_lt013g_legacy_iio'
for filename in ('t311_usb_role_diag', 't311_otg_interlock'):
    LINUX73_GENFS[f'/devices/platform/i2c-9/i2c-9/9-0066/max77693-charger/{filename}'] = 'sysfs_lt013g_otg_diag'
for filename in ('t311_otg_host_refresh', 't311_otg_host_stop'):
    LINUX73_GENFS[f'/devices/platform/i2c-9/i2c-9/9-0066/max77693-charger/{filename}'] = 'sysfs_lt013g_otg_control'

parser = argparse.ArgumentParser()
parser.add_argument('target_files', type=Path)
parser.add_argument('--require-linux73-policy', action='store_true')
parser.add_argument('--require-t311-usb-reconnect', action='store_true')
args = parser.parse_args()
if not args.target_files.is_file():
    parser.error('missing target-files ZIP')

with zipfile.ZipFile(args.target_files) as archive:
    names = set(archive.namelist())
    for image, expected in [('boot.img', EXPECTED_BOOT),
                            ('recovery.img', EXPECTED_RECOVERY)]:
        for folder in ('BOOTABLE_IMAGES', 'IMAGES'):
            name = f'{folder}/{image}'
            if name not in names:
                sys.exit(f'BLOCKED: {name} missing')
            with archive.open(name) as src:
                actual = hashlib.file_digest(src, 'sha256').hexdigest()
            if actual != expected:
                sys.exit(f'BLOCKED: {name} differs from candidate pin: {actual}')
            print(f'PASS {name} in {folder}')
    if any(p in names for p in ('SYSTEM/vendor/lib/egl/libGLES_mesa.so', 'VENDOR/lib/egl/libGLES_mesa.so')):
        sys.exit('BLOCKED: old Mesa 20 libGLES_mesa.so present')
    for rel, digest in EXPECTED.items():
        found = [p for p in (f'SYSTEM/vendor/{rel}', f'VENDOR/{rel}') if p in names]
        if len(found) != 1:
            sys.exit(f'BLOCKED: missing or duplicated {rel}')
        with archive.open(found[0]) as src:
            actual = hashlib.file_digest(src, 'sha256').hexdigest()
        if actual != digest:
            sys.exit(f'BLOCKED: {rel} SHA mismatch: {actual}')
        print(f'PASS {rel}')
    # Android 11's Build.isBuildConsistent() checks exact fingerprints of
    # /system and /vendor on this non-Treble SM-T311. A two-minute split
    # during multi-stage builds caused a persistent boot-time error dialog.
    def partition_prop(member, key):
        if member not in names:
            sys.exit(f'BLOCKED: missing build properties {member}')
        values = []
        for line in archive.read(member).decode('utf-8').splitlines():
            if line.startswith(key + '='):
                values.append(line.split('=', 1)[1].strip())
        if len(values) != 1:
            sys.exit(f'BLOCKED: {member} property {key} absent or duplicated')
        return values[0]
    system_fp = partition_prop('SYSTEM/build.prop', 'ro.system.build.fingerprint')
    vendor_fp = partition_prop('SYSTEM/vendor/build.prop', 'ro.vendor.build.fingerprint')
    if system_fp != vendor_fp:
        sys.exit('BLOCKED: Android boot error Build.isBuildConsistent(): '
                 f'SYSTEM fingerprint {system_fp} != VENDOR {vendor_fp}')
    print('PASS ANDROID11_SYSTEM_VENDOR_FINGERPRINT_CONSISTENT')

    if args.require_linux73_policy:
        cil_name = 'SYSTEM/vendor/etc/selinux/vendor_sepolicy.cil'
        binary_name = 'SYSTEM/vendor/etc/selinux/precompiled_sepolicy'
        if cil_name not in names or binary_name not in names:
            sys.exit('BLOCKED: Linux 7.3 SELinux policy files missing')
        cil = archive.read(cil_name)
        compiled = archive.read(binary_name)
        if len(LINUX73_GENFS) != 18:
            sys.exit('BLOCKED: unexpected Linux 7.3 label guard count')
        for path, label in LINUX73_GENFS.items():
            stanza = f'(genfscon sysfs {path} (u object_r {label} ((s0) (s0))))'.encode()
            if cil.count(stanza) != 1 or path.encode() not in compiled:
                sys.exit(f'BLOCKED: missing Linux 7.3 compiled SELinux label {path}')
        print('PASS LINUX73_SEPOLICY=18/18')
    if args.require_t311_usb_reconnect:
        daemon = 'SYSTEM/apex/com.android.adbd/bin/adbd'
        framework = 'SYSTEM/framework/services.jar'
        required = (
            daemon,
            framework,
            'SYSTEM/framework/oat/arm/services.art',
            'SYSTEM/framework/oat/arm/services.odex',
            'SYSTEM/framework/oat/arm/services.vdex',
        )
        if any(p not in names for p in required):
            sys.exit('BLOCKED: SM-T311 USB recovery or adbd ART payload missing')
        if b'SM-T311 secure Wi-Fi ADB port 5555 unavailable' not in archive.read(daemon):
            sys.exit('BLOCKED: secure TLS fixed-port adbd not packaged')
        with zipfile.ZipFile(io.BytesIO(archive.read(framework))) as jar:
            if not any(b'SM-T311 USB attached but gadget is disconnected' in jar.read(n)
                       for n in jar.namelist() if n.endswith('.dex')):
                sys.exit('BLOCKED: SM-T311 cable reattach services.jar not packaged')
            if not any(b'Suspending adbwifi' in jar.read(n)
                       for n in jar.namelist() if n.endswith('.dex')):
                sys.exit('BLOCKED: wireless debugging persistence source not packaged')
        print('PASS T311_USB_RECONNECT_SECURE_TLS_5555_AND_WIFI_PERSIST')
print('RELEASE_COMPONENTS=PASS')
