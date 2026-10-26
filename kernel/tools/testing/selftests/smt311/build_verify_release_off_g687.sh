#!/usr/bin/env bash
# G687: independently build vanilla SM-T311 RCU-OFF kernel from tracked
# defconfig and verify NO experimental code is linked in.
# Host-only. Does not flash/reboot or create a Samsung BOOT image.
set -euo pipefail

# G692: deterministic host-only kernel identity for repeatable SHA256.
# Does not alter kernelrelease/uname -r or any on-tablet BOOT contents.
# Rebuilding from the SAME source, compiler and root should now preserve
# UTS_VERSION instead of recording the current clock and auto #N.
export KBUILD_BUILD_TIMESTAMP="2026-10-21T00:00:00Z"
export KBUILD_BUILD_VERSION=1
export KBUILD_BUILD_USER="smt311"
export KBUILD_BUILD_HOST="reproducible"
cd "$(dirname "$0")/../../../.."
BUILD_DIR="../build/g687-release-rcu-off"
if [[ ! -f "$BUILD_DIR/.config" ]]; then
  make O="$BUILD_DIR" ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- sm_t311_android11_defconfig
fi

# This build must preserve BASE config. Refuse to silently reuse an
# experimental build directory or a compiler-output from another profile.
for disabled in SM_T311_RCU_HOT4K_EXPERIMENT PAGE_RCU_FREE DEBUG_FS; do
  if grep -Fxq "CONFIG_${disabled}=y" "$BUILD_DIR/.config"; then
    printf >&2 'FATAL: release build inadvertently enables %s\n' "$disabled"
    exit 4
  fi
done
grep -Fxq 'CONFIG_LOCALVERSION="-pjarczak"' "$BUILD_DIR/.config"
make -j6 O="$BUILD_DIR" ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- LOCALVERSION= zImage
python3 tools/testing/selftests/smt311/rcu_release_off_g687.py
python3 tools/testing/selftests/smt311/rcu_shmem_stub_g688.py
python3 tools/testing/selftests/smt311/rcu_iterator_scope_g689.py
python3 tools/testing/selftests/smt311/rcu_reference_budget_g693.py
python3 tools/testing/selftests/smt311/rcu_off_object_diff_g694.py
python3 tools/testing/selftests/smt311/rcu_physical_profile_g695.py
python3 tools/testing/selftests/smt311/rcu_boot_host_preflight_g696.py
python3 tools/testing/selftests/smt311/rcu_builtin_driver_abi_g697.py
python3 tools/testing/selftests/smt311/rcu_userspace_contract_g698.py
python3 tools/testing/selftests/smt311/rcu_boot_recovery_identity_g699.py
python3 tools/testing/selftests/smt311/rcu_debugfs_off_binary_g700.py
python3 tools/testing/selftests/smt311/rcu_debugfs_mount_contract_g701.py
python3 tools/testing/selftests/smt311/plan_check.py >/dev/null
printf 'G687: complete release OFF host link and RCU absence verified. NO FLASH.\n'
