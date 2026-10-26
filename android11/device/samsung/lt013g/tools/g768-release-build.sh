#!/usr/bin/env bash
# G768: fail-closed, portable SM-T311 Android11 SYSTEM release build.
# By DEFAULT checks source inputs and DOES NOT BUILD OR TOUCH THE TABLET.
# Explicit --build-system compiles and verifies a new SYSTEM image on the host;
# it is never authorization to flash, reboot, wipe, or change BOOT/RECOVERY.
set -euo pipefail

usage() {
    printf 'Usage: %s [--preflight | --build-system]\n' "${0##*/}" >&2
    exit 2
}

[ "$#" -le 1 ] || usage
mode="${1:---preflight}"
case "$mode" in --preflight|--build-system) ;; *) usage ;; esac

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
android="$(cd -- "$script_dir/../../../.." && pwd -P)"
device="$android/device/samsung/lt013g"
common="$android/device/samsung/lt01-common"
profile="$device/tools/g762-mke2fs-android11.conf"
board="$device/BoardConfig.mk"
boot_mk="$device/t311-custom-bootimg.mk"

[ -f "$android/build/envsetup.sh" ] || { echo 'G768 FAIL: missing Android build tree' >&2; exit 1; }
[ -f "$profile" ] || { echo 'G768 FAIL: G762 mke2fs profile missing' >&2; exit 1; }
[ -f "$board" ] && [ -f "$boot_mk" ] || { echo 'G768 FAIL: device recipe missing' >&2; exit 1; }
# The Android 11 host mke2fs 1.45.4 does not support orphan_file.
# The system's /etc/mke2fs.conf may contain orphan_file: never rely on it.
if grep -Eq '(^|[,[:space:]])orphan_file([,[:space:]]|$)' "$profile"; then
    echo 'G768 FAIL: orphan_file unsupported by Android mke2fs 1.45.4' >&2
    exit 1
fi
grep -Eq 'features = .*extent' "$profile" ||
    { echo 'G768 FAIL: expected ext4 feature profile missing' >&2; exit 1; }
grep -Fq 'BUILD_BROKEN_NINJA_USES_ENV_VARS += MKE2FS_CONFIG' "$board" ||
    { echo 'G768 FAIL: AOSP build does not allow MKE2FS_CONFIG' >&2; exit 1; }
grep -Fq 'T311_BOOT_VARIANT ?= g958-current' "$boot_mk" ||
    { echo 'G768 FAIL: G958 BOOT is not build default' >&2; exit 1; }
if [ -n "${T311_BOOT_VARIANT:-}" ] && [ "$T311_BOOT_VARIANT" != "g958-current" ]; then
    echo 'G768 FAIL: refuse non-G958 BOOT variant in production release' >&2
    exit 1
fi
python3 "$device/tools/g960-source-integrity.py" --source --boot
python3 "$common/tools/g767-verify-known-good-build-inputs.py"
python3 "$device/tools/g769-verify-private-rom-inputs.py"

echo "G768_PREFLIGHT_PASS: physical G958 BOOT + TWRP + WiFi/USB prebuilts + Android11 ext4 config"
if [ "$mode" = "--preflight" ]; then
    echo 'G768_NO_BUILD_NO_FLASH: pass --build-system explicitly to compile a new ROM'
    exit 0
fi

# G994: Before allocating an entire SYSTEM image, compare *every* critical
# freshly built component against its independently verified tablet readback.
# Prior G960 --built only pins HOST binaries and therefore can PASS when the
# selected libril.so differs from the one safely running on the tablet.
# Keep G964 as an independent second fail-closed modem check below.
python3 "$device/tools/g994-physical-release-gate.py" --mode full-rom
# Modem/PIN/PUK changes require a separately qualified radio update;
# do not silently promote a fresh libril when building SYSTEM.
python3 "$device/tools/g964-modem-freeze.py" --built

export MKE2FS_CONFIG="$profile"
export T311_BOOT_VARIANT=g958-current
# This older LineageOS 18.1 tree invokes /usr/bin/env python via legacy
# host-side scripts. Debian testing ships python3 but may omit python.
# Supply a temporary *host-only* compatibility command with no global changes.
if ! command -v python >/dev/null 2>&1; then
    python_bin="$(command -v python3)"
    [ -n "$python_bin" ] || { echo 'G768 FAIL: python3 not found' >&2; exit 1; }
    compat_dir="$(mktemp -d -t g768-python3-XXXXXX)"
    ln -s -- "$python_bin" "$compat_dir/python"
    export PATH="$compat_dir:$PATH"
    trap 'rm -f -- "$compat_dir/python"; rmdir -- "$compat_dir"' EXIT
fi
cd "$android"
# SOURCE + LUNCH + M use the checked-in profile throughout APEX and SYSTEM ext4.
# No clean or output removal: incremental compiles preserve existing results.
# shellcheck disable=SC1091
# Android 11 envsetup and its m function reference optional unset shell
# variables (ZSH_VERSION, TOP). Keep nounset disabled ONLY during these
# legacy build functions; explicitly check ALL return codes, because sourcing
# can also change Bash's errexit flags. Never accept a failed build as PASS.
set +u
source build/envsetup.sh >/dev/null || { echo 'G768 FAIL: envsetup' >&2; exit 1; }
lunch lineage_lt013g-userdebug >/dev/null || { echo 'G768 FAIL: lunch' >&2; exit 1; }
[ "$(gettop)" = "$android" ] && [ "$TARGET_PRODUCT" = 'lineage_lt013g' ] ||
    { echo 'G768 FAIL: wrong lunch/build root' >&2; exit 1; }
m -j3 systemimage || { echo 'G768 FAIL: Android systemimage compilation' >&2; exit 1; }
set -euo pipefail

python3 "$device/tools/g960-source-integrity.py" --source --boot --built
python3 "$common/tools/g767-verify-known-good-build-inputs.py" --check-built

# Build artifact must be as new as the freshly compiled framework dependency.
# Without this gate, G768 may validate a previously accepted G761 system.img
# even when the new framework is NOT embedded in that stale image.
python3 - "$android" <<'PYFRESH'
from pathlib import Path
import sys
root = Path(sys.argv[1])
product = root / 'out/target/product/lt013g'
system = product / 'system.img'
framework = product / 'system/framework/framework.jar'
if (not system.is_file() or not framework.is_file()
        or system.stat().st_mtime_ns < framework.stat().st_mtime_ns):
    raise SystemExit('G768 FAIL: SYSTEM image missing or older than framework.jar')
print('G768_SYSTEMIMAGE_FRESHNESS_PASS: current Android framework packaged')
PYFRESH

# Before expensive 2.2 GB sparse-image QA, reject all stale host APK/ODEX/SEPolicy inputs.
python3 "$device/tools/g963-verify-host-image-pins.py"
python3 "$device/tools/g972-system-art-triplet-gate.py"

# Critical: a successful compile alone is not a flashable release.
# G760 QA checks ext4 geometry, clean fs state, 16 physical reference
# binaries, UID/GID/mode, and SELinux labels/xattrs. Fail on any mismatch.
qa_path="$android/out/target/product/lt013g/g768-systemimage-qa.json"
python3 "$device/tools/g755-verify-systemimage-contents.py" --out "$qa_path" ||
    { echo 'G768 FAIL: strict 16-file image verifier' >&2; exit 1; }
python3 - "$android" "$qa_path" <<'PY'
from pathlib import Path
import json
import sys

root = Path(sys.argv[1])
qa_path = Path(sys.argv[2])
if not qa_path.is_file():
    raise SystemExit("G768 FAIL: missing strict G760 QA report")
qa = json.loads(qa_path.read_text())
if (qa.get("result") != "PASS"
        or qa.get("verified_file_count") != 16
        or qa.get("system_partition_bytes") != 2235564032
        or qa.get("filesystem_clean") is not True
        or Path(qa.get("source", "")).resolve()
           != (root / "out/target/product/lt013g/system.img").resolve()):
    raise SystemExit("G768 FAIL: mandatory G760 16-file image QA failed")
print("G768_SYSTEMIMAGE_QA_PASS; NO FLASH AUTHORIZATION", qa["image_sha256"])
PY

# G964: user froze modem and SIM; never release a different RIL binary.
python3 "$device/tools/g964-modem-freeze.py" --built
