#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ] || [ ! -n "$1" ]; then
    echo 'usage: g1486-build-source-g1130-hostonly.sh ABSOLUTE_OUTPUT_DIRECTORY' >&2
    exit 20
fi

kernel="$(cd "$(dirname "$0")/../.." && pwd -P)"
command -v realpath >/dev/null
output="$(realpath -m -- "$1")"
case "$output" in
    /*) ;;
    *) echo 'G1486_REQUIRES_ABSOLUTE_BUILD_OUTPUT_PATH' >&2; exit 21 ;;
esac
case "$output" in
    /|/home|/home/debian|/usr|/etc|/var|/bin|/sbin|/lib|/lib64|/boot|/root|/dev|/proc|/sys|/data|/mnt|/media|/opt|/run|/tmp|/dev/*|/proc/*|/sys/*|/data/*|"$kernel"|"$kernel/"*)
        echo 'G1486_UNSAFE_OUTPUT_PATH' >&2; exit 22 ;;
esac

command -v arm-linux-gnueabihf-gcc >/dev/null
command -v arm-linux-gnueabihf-ld >/dev/null
command -v python3 >/dev/null
[ -z "$(git -C "$kernel" status --porcelain --untracked-files=no)" ] || exit 23
python3 "$kernel/scripts/smt311/verify_g1481_accepted_g1130_source.py" >/dev/null

export TZ=Europe/Warsaw
export KBUILD_BUILD_TIMESTAMP='Sat Oct 24 16:54:34 CEST 2026'
export KBUILD_BUILD_VERSION=1
export KBUILD_BUILD_USER=debian
export KBUILD_BUILD_HOST=debian

make -s -C "$kernel" O="$output" ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- sm_t311_android11_defconfig
python3 "$kernel/scripts/smt311/verify_g1467_physical_kernel_config.py" --generated-production "$output/.config" >/dev/null
make -s -j4 -C "$kernel" O="$output" ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- LOCALVERSION= zImage dtbs

(
    cd "$output"
    sh "$kernel/usr/gen_initramfs.sh" -o usr/initramfs_data.cpio \
        -l usr/.initramfs_data.cpio.d \
        -d 'Sat Oct 24 16:49:41 CEST 2026' "$kernel/usr/default_cpio_list"
)
[ "$(sha256sum "$output/usr/initramfs_data.cpio" | cut -d' ' -f1)" = \
  f0feae07c6dfcb6c2362c921dc8ddafbf85d902b627afee50be46d39365c1cc6 ] || exit 24

make -s -j4 -C "$kernel" O="$output" ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- LOCALVERSION= zImage dtbs
[ "$(sha256sum "$output/usr/initramfs_data.cpio" | cut -d' ' -f1)" = \
  f0feae07c6dfcb6c2362c921dc8ddafbf85d902b627afee50be46d39365c1cc6 ] || exit 25

python3 "$kernel/scripts/smt311/verify_g1481_accepted_g1130_source.py" \
    --production-output "$output" >/dev/null

printf 'G1493_ACCEPTED_G1130_SOURCE_ARM32_BUILD_PASS_NO_FLASH\n'
sha256sum "$output/arch/arm/boot/zImage" "$output/vmlinux" "$output/init/version.o" "$output/init/version-timestamp.o"
[ "$(sha256sum "$output/init/version.o" | cut -d' ' -f1)" = bdfe382f6520aa0f709ca8bf8d97c9fb31b616ff2c6dc581735e5e1cbcaf7816 ] || exit 32
[ "$(sha256sum "$output/init/version-timestamp.o" | cut -d' ' -f1)" = a6368b765090ded1080888e0b24a7ec0a534dd343b8f999572b1089731452348 ] || exit 33
[ "$(sha256sum "$output/vmlinux" | cut -d' ' -f1)" = 63c180b446107824e5b8617a07af9cd7ae4d9e3a6e5ac1dca922c488dada4b0c ] || exit 34
[ "$(sha256sum "$output/arch/arm/boot/zImage" | cut -d' ' -f1)" = 4f6d00ea7230ad6a0a43311bb09ace7451b430698d126eb059279e11feedbea6 ] || exit 42
printf 'G1493_PHYSICALLY_ACCEPTED_G1130_ZIMAGE_AND_VMLINUX_BIT_EXACT_SOURCE_REBUILD_PASS_NO_FLASH\n'
