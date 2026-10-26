#!/usr/bin/env bash
# Build a single-identify Android11 SM-T311 SYSTEM+VENDOR release. No flashing.
set -eo pipefail
ROOT=/home/debian/android/lineage-18.1-sm-t311
cd "$ROOT"
export BUILD_NUMBER=SMT311G591
export PATH="/home/debian/Projekty/samsung/tools/smt311/aosp-python3-shim:$PATH"
source build/envsetup.sh >/dev/null
lunch lineage_lt013g-userdebug >/dev/null
OUT=out/target/product/lt013g
ZIP=$OUT/obj/PACKAGING/target_files_intermediates/lineage_lt013g-target_files-$BUILD_NUMBER.zip
# G585 diagnostics are deliberately NOT part of the production image.
# Explicit module builds leave APK/ODEX/VDEX in the output system tree;
# target-files-package can accidentally include all three. Delete only
# this known generated test package, never source files or user data.
for PROBE_NAME in T311GnssStatus T311Camera2Smoke; do
PROBE_DIR="$OUT/system/product/app/$PROBE_NAME"
if [[ -d "$PROBE_DIR" ]]; then
  python3 - "$PROBE_DIR" <<'CLEAN'
from pathlib import Path
import shutil
import sys
d=Path(sys.argv[1])
assert d.name in {'T311GnssStatus','T311Camera2Smoke'}
assert d.resolve().as_posix().endswith(
    '/out/target/product/lt013g/system/product/app/' + d.name)
allowed={d.name+'.apk',d.name+'.odex',d.name+'.vdex'}
names={f.name for f in d.rglob('*') if f.is_file()}
if not names or not names.issubset(allowed):
    raise SystemExit('G591_BLOCKED_UNEXPECTED_DIAGNOSTIC_APK_CONTENTS')
shutil.rmtree(d)
print('G591_EPHEMERAL_GNSS_TEST_APP_REMOVED_FROM_GENERATED_OUTPUT')
CLEAN
fi
done
# These are GENERATED output properties only, never vendor sources/user data.
# They were created during different minutes in G567 and must rebuild together.
for generated in \
 "$OUT/obj/ETC/system_build_prop_intermediates/build.prop" \
 "$OUT/system/build.prop" \
 "$OUT/system/vendor/build.prop"; do
 if [[ -f "$generated" ]]; then rm -- "$generated"; fi
done
m -j2 target-files-package
python3 device/samsung/lt013g/tools/check_release_components.py  --require-linux73-policy --require-t311-usb-reconnect "$ZIP"
# Prevent an accidental stale /vendor HAL from silently excluding G591.
python3 - "$ZIP" "$OUT/system/vendor/lib/hw/gps.default.so" <<'CHECK'
import hashlib
import sys
from pathlib import Path
from zipfile import ZipFile
bundle, built = sys.argv[1:]
with ZipFile(bundle) as archive:
    if any(('T311GnssStatus' in name or 'T311Camera2Smoke' in name) for name in archive.namelist()):
        raise SystemExit("G591_FAIL_PROBE_APP_MUST_NOT_SHIP")
    policy = archive.read("SYSTEM/vendor/etc/selinux/vendor_sepolicy.cil").decode()
    checks = [
        "(type mediacodec_tmpfs)",
        "(typetransition mediacodec tmpfs_30_0 file mediacodec_tmpfs)",
        "(allow mediacodec mediacodec_tmpfs (file (read write getattr map)))",
    ]
    if not all(rule in policy for rule in checks):
        raise SystemExit("G591_FAIL_CODEC2_PRIVATE_MEMFD_SEPOLICY")
    print("G591_CODEC2_FMQ_PRIVATE_MEMFD_SEPOLICY_IN_BUNDLE_PASS")
    target = archive.read("SYSTEM/vendor/lib/hw/gps.default.so")
    sys_fp = next(line for line in archive.read("SYSTEM/build.prop").decode().splitlines()
                  if line.startswith("ro.system.build.fingerprint=")).split("=",1)[1]
    vendor_fp = next(line for line in archive.read("SYSTEM/vendor/build.prop").decode().splitlines()
                     if line.startswith("ro.vendor.build.fingerprint=")).split("=",1)[1]
    if sys_fp != vendor_fp or "/SMT311G591:" not in sys_fp:
        raise SystemExit("G591_FAIL_FINGERPRINT_CONSISTENCY")
if target != Path(built).read_bytes():
    raise SystemExit("G591_FAIL_GPS_HAL_ARTIFACT_MISMATCH")
print("G591_SAME_GPS_HAL_IN_VERIFIED_TARGET_FILES_PASS")
print("G591_FINGERPRINT_SYSTEM_VENDOR_EQUAL_PASS")
print("G591_GPS_HAL_SHA256", hashlib.sha256(target).hexdigest())
CHECK
sha256sum "$ZIP"
