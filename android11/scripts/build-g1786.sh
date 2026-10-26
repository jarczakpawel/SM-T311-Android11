#!/usr/bin/env bash
# SM-T311 Android11 exact G1786 public source build configuration.
# Proprietary firmware/ELF and SHA-pinned Mesa26.2.5 outputs must be provided
# from a legally obtained matching local installation before running this.
set -euo pipefail
if [[ $# -ne 1 ]]; then echo "Usage: $0 /path/to/android11" >&2; exit 2; fi
A=$(realpath "$1")
test -d "$A/.repo" || { echo "Missing repo checkout" >&2; exit 3; }
test -f "$A/device/samsung/lt013g/t311-platform.mk"
test -f "$A/device/samsung/lt01-common/common.mk"
test -f "$A/device/samsung/lt013g/tools/g1150-android11-mke2fs-2019-compat.conf"
GALLIUM="$A/device/samsung/lt01-common/mesa26-g816/experimental-g1749/libgallium_dri.so"
test -f "$GALLIUM" || { echo "Missing private/local Mesa ARM32 output: $GALLIUM" >&2; exit 4; }
printf '%s  %s\n' 'fe8a15d701915f0178114d095555a2eb4d54dae95d5bbabd6f61a4d80639a3e0' "$GALLIUM" | sha256sum -c -
# Use pinned Python2 interpreter only for legacy Android11 generators.
SHIM="$A/.smt311-python-shim"
mkdir -p "$SHIM"
cat > "$SHIM/python" <<'SH'
#!/bin/sh
case "${1##*/}" in
    java-event-log-tags.py)
        exec "$SMT311_AOSP_SOURCE/prebuilts/python/linux-x86/2.7.5/bin/python2" "$@"
        ;;
esac
exec /usr/bin/python3 "$@"
SH
cat > "$SHIM/python2.7" <<'SH'
#!/bin/sh
exec "$SMT311_AOSP_SOURCE/prebuilts/python/linux-x86/2.7.5/bin/python2.7" "$@"
SH
chmod +x "$SHIM/python" "$SHIM/python2.7"
export SMT311_AOSP_SOURCE="$A"
export PATH="$SHIM:$PATH"
export BUILD_NUMBER=SMT311G1441
export BUILD_DATETIME=1792932650
export SMT311_PINNED_LINEAGE_DATE=20261025
export SMT311_PINNED_FILE_TAG=SMT311G1786
export SMT311_MESA_GLPB_OPTIN=true
export MKE2FS_CONFIG="$A/device/samsung/lt013g/tools/g1150-android11-mke2fs-2019-compat.conf"
cd "$A"
source build/envsetup.sh
lunch lineage_lt013g-userdebug
m -j4 systemimage target-files-package
echo "Built target-files; compare SYSTEM/BOOT/RECOVERY to images/SHA256SUMS.txt"
