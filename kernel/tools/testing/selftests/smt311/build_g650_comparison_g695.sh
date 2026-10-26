#!/usr/bin/env bash
# G695: rebuild G650 comparison .o files independently from local Git.
# Build-only: NO adb, TWRP, flash, Samsung BOOT packaging or backups.
# This reconstructs the inputs used by rcu_off_object_diff_g694.py
# and rcu_physical_profile_g695.py when checking out a new repository.
set -euo pipefail

export KBUILD_BUILD_TIMESTAMP="2026-10-21T00:00:00Z"
export KBUILD_BUILD_VERSION=1
export KBUILD_BUILD_USER="smt311"
export KBUILD_BUILD_HOST="reproducible"

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/../../../.."
KERNEL_ROOT="$(pwd)"
SRC="$KERNEL_ROOT/../comparison/g695-g650-experiment-source"
BASE_ON="$KERNEL_ROOT/../comparison/g695-g650-experiment-build"
BASE_OFF="$KERNEL_ROOT/../comparison/g694-g650-build"
CURRENT_ON="$KERNEL_ROOT/../build/g686-reproducible-exp-on"
CURRENT_OFF="$KERNEL_ROOT/../build/g687-release-rcu-off"
G650="a2fca4558"

test -f "$CURRENT_ON/.config"
test -f "$CURRENT_OFF/.config"
test "$(git cat-file -t "$G650")" = commit

if [ -e "$SRC" ]; then
    printf >&2 'G695: refuse to overwrite existing source worktree: %s\n' "$SRC"
    exit 3
fi
mkdir -p "$BASE_ON" "$BASE_OFF"
git worktree add --detach "$SRC" "$G650"
cleanup() {
    # Only remove the temp worktree created by THIS script, never the
    # persistent compiled old-source comparison object files.
    git worktree remove "$SRC"
}
trap cleanup EXIT
test "$(git -C "$SRC" rev-parse --short=9 HEAD)" = "a2fca4558"

for profile in EXP OFF; do
    if [ "$profile" = EXP ]; then
        REFERENCE="$CURRENT_ON"
        OUTPUT="$BASE_ON"
        FILES=(mm/filemap.o mm/shmem.o mm/page_alloc.o mm/vma.o)
    else
        REFERENCE="$CURRENT_OFF"
        OUTPUT="$BASE_OFF"
        FILES=(mm/filemap.o mm/shmem.o)
    fi
    cp "$REFERENCE/.config" "$OUTPUT/.config"
    make -s -C "$SRC" O="$OUTPUT" ARCH=arm \
        CROSS_COMPILE=arm-linux-gnueabihf- LOCALVERSION= olddefconfig
    cmp "$REFERENCE/.config" "$OUTPUT/.config" || {
        printf >&2 'G695: nonidentical .config after olddefconfig for %s\n' "$profile"
        exit 4
    }
    make -s -j6 -C "$SRC" O="$OUTPUT" ARCH=arm \
        CROSS_COMPILE=arm-linux-gnueabihf- LOCALVERSION= "${FILES[@]}"
    echo "G650 $profile source objects reproduced from $G650"
    for f in "${FILES[@]}"; do
        test -s "$OUTPUT/$f"
    done
done

python3 tools/testing/selftests/smt311/rcu_off_object_diff_g694.py
python3 tools/testing/selftests/smt311/rcu_physical_profile_g695.py
echo "G695: G650 SOURCE reconstructed in both host profiles, real ARM32 checks PASS."
echo "G695: worktree auto-cleanup will retain only diagnostic .o objects. NO FLASH."
