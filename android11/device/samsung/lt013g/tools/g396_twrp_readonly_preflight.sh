#!/sbin/sh
# SM-T311 G396 TWRP read-only preflight for cumulative signed G396.
# Never FLASH/reboot/mount/chcon/change SIM/modify any device or partition.
# To run ONLY AFTER manual hardware-key boot into TWRP:
# /sbin/sh /data/local/tmp/SM-T311-G396-TWRP-READONLY-PREFLIGHT.sh
# If /data cannot be mounted, STOP and arrange owner-supervised TWRP
# sideload/external SD instead. Never put a 484MB ZIP into TWRP tmpfs RAM.
set -eu
bb=/sbin/busybox
signed=/data/local/tmp/SM-T311-G396-GUARDED-GPS-ROUTE-BCB-SUPL-SIGNED.zip
want=a6fcf6db0bb15f64df46e5cb3bd48f43e383fdd00bc80fd63a575dc0695a507d
boot=/dev/block/mmcblk0p9
system=/dev/block/mmcblk0p20
bootlink=/dev/block/platform/dw_mmc/by-name/BOOT
systemlink=/dev/block/platform/dw_mmc/by-name/SYSTEM
known=cad5e76faf372beff3243ec615924286096713c994a6e5a03d3231f69478e6fd
old=f9292bab437bce12cea56e6e0c067a15c87475b69a61fc0c13fd4d3f07b9694f
die() { echo "G396 READ-ONLY BLOCK: $1" >&2; exit 1; }
echo "G396 SM-T311 preflight ONLY. No write/flash/reboot will be performed."
[ -x "$bb" ] || die "installed recovery BusyBox absent"
[ -x /sbin/twrp ] || die "not booted into expected TWRP recovery"
[ -L /sbin/sh ] || die "stock TWRP /sbin/sh symlink missing"
[ "$(id -u)" = "0" ] || die "recovery process not root"
[ -b "$boot" ] || die "physical BOOT p9 missing"
[ -b "$system" ] || die "physical SYSTEM p20 missing"
# Check EXACT aliases expected by this already-signed G396 updater-script,
# NOT alternate Android /dev/block/platform/soc/... aliases.
[ -L "$bootlink" ] || die "TWRP old dw_mmc BOOT alias absent; signed guard would fail"
[ -L "$systemlink" ] || die "TWRP old dw_mmc SYSTEM alias absent; signed guard would fail"
[ "$("$bb" readlink -f "$bootlink")" = "$boot" ] || die "BOOT does not resolve to p9"
[ "$("$bb" readlink -f "$systemlink")" = "$system" ] || die "SYSTEM does not resolve to p20"
# block_image_update requires SYSTEM not to be mounted by recovery.
if "$bb" awk '$2=="/system" || $2=="/system_root" {found=1} END{exit !found}' /proc/mounts; then
    die "SYSTEM is mounted; STOP. Unmount manually in TWRP before install"
fi
# Only read the 6838272-byte BOOT prefix, exactly how the signed guard does.
actual="$("$bb" dd if="$boot" bs=2048 count=3339 2>/dev/null | "$bb" sha256sum | "$bb" cut -d' ' -f1)"
[ "$actual" = "$known" ] || [ "$actual" = "$old" ] ||
    die "unknown physical BOOT prefix. Refuse to continue"
echo "G396 verified BOOT block p9 exact expected prefix SHA."
# /data is unencrypted under current G379, but actual recovery mount and
# availability still require a real-world check (this is deliberately strict).
[ -f "$signed" ] || die "signed G396 ZIP not accessible through /data; STOP for safe owner sideload"
actualzip="$("$bb" sha256sum "$signed" | "$bb" cut -d' ' -f1)"
[ "$actualzip" = "$want" ] || die "signed G396 ZIP SHA differs; refuse"
echo "G396 signed G396 ZIP exact SHA PASS."
echo "G396 ALL READ-ONLY SAFETY GATES PASS. This did NOT install anything."
echo "MANUAL TWRP INSTALL STILL REQUIRES OWNER CONSENT AND CONTROL."
