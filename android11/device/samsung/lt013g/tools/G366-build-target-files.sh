#!/usr/bin/env bash
# Build SM-T311 Android 11 target-files with local e2fsprogs 1.45.4 compatibility.
# Does not modify /etc/mke2fs.conf or the live tablet.
set -eo pipefail
workers="${1:-3}"
if [[ ! "$workers" =~ ^[1-9][0-9]*$ ]] || ((workers > 12)); then
  echo "Usage: $0 [parallel_jobs_1_to_12]" >&2
  exit 2
fi
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$root"
# Debian forky supplies python3 but may not ship a /usr/bin/python alias.
# The Android 11 build still invokes /usr/bin/env python in legacy generators.
# Provide a local build-only shim; never write to /usr/bin or host settings.
shimdir="$root/out/g366-build-shims/bin"
mkdir -p "$shimdir"
ln -sfn "$(command -v python3)" "$shimdir/python"
export PATH="$shimdir:$PATH"
export MKE2FS_CONFIG="$root/device/samsung/lt013g/tools/mke2fs-android11-compat.conf"
[[ -f "$MKE2FS_CONFIG" ]] || { echo "MKE2FS_CONFIG missing" >&2; exit 3; }
echo "G366: source=$root"
echo "G366: using build-only e2fs config=$MKE2FS_CONFIG"
source build/envsetup.sh >/dev/null
lunch lineage_lt013g-eng >/dev/null
m "-j$workers" target-files-package
