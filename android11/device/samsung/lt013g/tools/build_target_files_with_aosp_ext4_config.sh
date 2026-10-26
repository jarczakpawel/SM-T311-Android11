#!/usr/bin/env bash
# Samsung SM-T311 Android 11 incremental target-files build.
# Host Debian testing /etc/mke2fs.conf enables orphan_file unsupported by
# AOSP's bundled e2fsprogs 1.45.4. Use the AOSP-owned compatible config.
set -eo pipefail
cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/../../../.."
if [[ ! -f system/extras/ext4_utils/mke2fs.conf ]] || [[ ! -x out/soong/host/linux-x86/bin/mke2fs ]]; then
  echo 'SM-T311: missing bundled Android 11 ext4 config or bundled mke2fs' >&2
  exit 3
fi
# Debian testing no longer ships /usr/bin/python. Android 11 requires its
# prebuilts (py2-cmd and py3-cmd) rather than assuming a host python alias.
export PATH="$PWD/prebuilts/build-tools/path/linux-x86:$PATH"
export MKE2FS_CONFIG="$PWD/system/extras/ext4_utils/mke2fs.conf"
echo "SM-T311 AOSP MKE2FS_CONFIG=$MKE2FS_CONFIG"
source build/envsetup.sh >/dev/null
lunch lineage_lt013g-userdebug >/dev/null
# The OTA package is a BUILD-ONLY candidate. It must pass separate install and
# independent rollback checks before use on a real tablet.
goal="${1:-target-files-package}"
case "$goal" in
  target-files-package|otapackage) ;;
  *) echo "Unsupported goal: $goal" >&2; exit 2;;
esac
m "$goal" -j3
