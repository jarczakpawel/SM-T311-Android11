#!/usr/bin/env bash
set -Eeuo pipefail

HOME_DIR=${HOME}
KERNEL_ROOT=${KERNEL_TREE:-$HOME_DIR/Projekty/samsung/kernel}
ANDROID_ROOT=${ANDROID_TREE:-$HOME_DIR/android/lineage-18.1-sm-t311}
KERNEL_JOBS=${SMT311_JOBS:-6}
ANDROID_JOBS=${ANDROID_JOBS:-3}

cd "$KERNEL_ROOT"
SMT311_JOBS="$KERNEL_JOBS" ./tools/smt311/build-source-boot.sh development
./tools/smt311/sync-android-prebuilt.sh

cd "$ANDROID_ROOT"
export TARGET_UNOFFICIAL_BUILD_ID=PJARCZAK
set +u
source build/envsetup.sh
set -u
lunch lineage_lt013g-userdebug
mka bacon -j"$ANDROID_JOBS"
