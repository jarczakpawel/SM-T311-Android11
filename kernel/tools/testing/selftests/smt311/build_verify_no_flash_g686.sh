#!/usr/bin/env bash
# SM-T311 G686: full ARM32 host-only kernel link, using TRACKED defconfig
# plus TRACKED experimental config fragment, with exact kernel release name.
# No adb/fastboot/Heimdall/TWRP, no flash, no BOOT packaging or backups.
set -euo pipefail

# G692: deterministic host-only kernel identity for repeatable SHA256.
# Does not alter kernelrelease/uname -r or any on-tablet BOOT contents.
# Rebuilding from the SAME source, compiler and root should now preserve
# UTS_VERSION instead of recording the current clock and auto #N.
export KBUILD_BUILD_TIMESTAMP="2026-10-21T00:00:00Z"
export KBUILD_BUILD_VERSION=1
export KBUILD_BUILD_USER="smt311"
export KBUILD_BUILD_HOST="reproducible"

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/../../../.."

BUILD_DIR="${SMT311_BUILD_DIR:-../build/g686-reproducible-exp-on}"
BASE="arch/arm/configs/sm_t311_android11_defconfig"
FRAGMENT="arch/arm/configs/sm_t311_rcu_experiment_g686.fragment"
CONFIG="$BUILD_DIR/.config"
test -f "$BASE"
test -f "$FRAGMENT"

# Explicit fragment is retained in Git for eventual GitHub publication.
# Kconfig's SM_T311 experiment depends on DEBUG_FS and selects PAGE_RCU_FREE.
for item in \
  CONFIG_DEBUG_FS=y \
  CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y \
  CONFIG_PAGE_RCU_FREE=y \
  'CONFIG_LOCALVERSION="-pjarczak"' \
  '# CONFIG_LOCALVERSION_AUTO is not set'; do
  grep -Fxq -- "$item" "$FRAGMENT" || {
    printf >&2 'FATAL: experimental fragment missing: %s\n' "$item"
    exit 3
  }
done

# New build: create the .config from the tracked standard tablet defconfig.
# Subsequent builds retain the existing .config but require its exact flags.
if [[ ! -f "$CONFIG" ]]; then
  make O="$BUILD_DIR" ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- sm_t311_android11_defconfig
  scripts/config --file "$CONFIG" \
    --enable DEBUG_FS \
    --enable SM_T311_RCU_HOT4K_EXPERIMENT \
    --enable PAGE_RCU_FREE \
    --set-str LOCALVERSION "-pjarczak" \
    --disable LOCALVERSION_AUTO
  make O="$BUILD_DIR" ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- olddefconfig
fi

for item in \
  CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y \
  CONFIG_PAGE_RCU_FREE=y \
  CONFIG_DEBUG_FS=y \
  CONFIG_ARCH_EXYNOS4=y \
  'CONFIG_LOCALVERSION="-pjarczak"'; do
  grep -Fxq -- "$item" "$CONFIG" || {
    printf >&2 'FATAL: Kconfig did not enable the required option: %s\n' "$item"
    exit 4
  }
done

# Explicit LOCALVERSION= stops Kbuild setlocalversion from adding '+' to
# the requested name even though CONFIG_LOCALVERSION_AUTO is disabled.
make -j6 O="$BUILD_DIR" ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- LOCALVERSION= zImage

export SMT311_BUILD_DIR="$BUILD_DIR"
python3 tools/testing/selftests/smt311/rcu_linked_image_gate_g686.py

# All existing source/RCU and release-OFF gates through G688.
for stage in 659 660 661 666 667 673 674 675 676 677 678 679 680 681 682 683 684 685 687 688 689 690 691 692 693 694 695 696 697 698 699 700 701; do
  matches=(tools/testing/selftests/smt311/rcu_*g"$stage".py)
  if (( ${#matches[@]} != 1 )) || [[ ! -f "${matches[0]}" ]]; then
    printf >&2 'FATAL: expected a unique regression for G%s\n' "$stage"
    exit 5
  fi
  python3 "${matches[0]}" >/dev/null
  printf 'G%s regression PASS\n' "$stage"
done
python3 tools/testing/selftests/smt311/plan_check.py >/dev/null
printf 'S0-S8 plan PASS\n'
printf 'G686: HOST FULL-LINK VERIFIED ONLY. NOT A FLASHABLE SAMSUNG BOOT/TWRP IMAGE.\n'
