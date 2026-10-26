#!/usr/bin/env bash
# G1150: resumable native Android11 target-files after host e2fsprogs mismatch.
# NO cleaning, backup, flashing, SIM PIN, sound or GitHub push.
set -euo pipefail
ROOT=/home/debian/android/lineage-18.1-sm-t311
CONF="$ROOT/device/samsung/lt013g/tools/g1150-android11-mke2fs-2019-compat.conf"
if [[ "${1:-}" != "--native-apex-failure-confirmed" ]]; then
  echo "G1150_REFUSE: confirm prior native job FAILED before attempting resume" >&2
  exit 20
fi
if [[ ! -f "$CONF" || -L "$CONF" ]]; then
  echo "G1150_REFUSE: source-committed APEX mke2fs compat config missing" >&2
  exit 21
fi
if grep -Eq '^[[:space:]]*features[[:space:]]*=.*orphan_file' "$CONF"; then
  echo "G1150_REFUSE: source config still requests unsupported orphan_file" >&2
  exit 22
fi
# Mandatory operator tool check must independently confirm the old job
# really stopped: workspace_run has isolated PID namespace; ps alone is NOT
# adequate proof of no concurrent builders.
export MKE2FS_CONFIG="$CONF"
export PATH="/home/debian/Projekty/samsung/tools/smt311/aosp-python3-shim:$PATH"
cd "$ROOT"
# AOSP envsetup.sh references unset ZSH_VERSION and others; disable
# nounset ONLY at the Android build-script boundary, preserve errexit.
set +u
source build/envsetup.sh >/dev/null
lunch lineage_lt013g-userdebug >/dev/null
echo G1150_APEX_MKE2FS_COMPAT_CONFIG_ACTIVE
echo "G1150_RESUMING_NATIVE_AFTER_APEX_FAILURE_WITHOUT_CLEAN"
m -j4 target-files-package
echo G1150_NATIVE_INCREMENTAL_TARGET_FILES_SUCCEEDED_NOT_YET_RELEASE_QUALIFIED
