#!/usr/bin/env bash
# Build a single-identify Android11 SM-T311 SYSTEM+VENDOR release. No flashing.
set -eo pipefail
ROOT=/home/debian/android/lineage-18.1-sm-t311
cd "$ROOT"
export BUILD_NUMBER=SMT311G573
export PATH="/home/debian/Projekty/samsung/tools/smt311/aosp-python3-shim:$PATH"
source build/envsetup.sh >/dev/null
lunch lineage_lt013g-userdebug >/dev/null
OUT=out/target/product/lt013g
ZIP=$OUT/obj/PACKAGING/target_files_intermediates/lineage_lt013g-target_files-$BUILD_NUMBER.zip
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
sha256sum "$ZIP"
