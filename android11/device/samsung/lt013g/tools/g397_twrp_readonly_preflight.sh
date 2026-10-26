#!/sbin/sh
# SM-T311 G397 TWRP read-only preflight for cumulative signed G397.
# Never FLASH/reboot/mount/chcon/change SIM/modify any device or partition.
# To run ONLY AFTER manual hardware-key boot into TWRP:
# /sbin/sh /data/local/tmp/SM-T311-G397-TWRP-READONLY-PREFLIGHT.sh
# If /data cannot be mounted, STOP and arrange owner-supervised TWRP
# sideload/external SD instead. Never put a 484MB ZIP into TWRP tmpfs RAM.
set -eu
bb=/sbin/busybox
signed=/data/local/tmp/SM-T311-G397-GUARDED-GPS-ROUTE-BCB-SUPL-POLL-SIGNED.zip
want=e949e7ff86dc6e95f242a1d97b99dcc11363dc015e79c5535e2f400a8f27f6ab
boot=/dev/block/mmcblk0p9
system=/dev/block/mmcblk0p20
bootlink=/dev/block/platform/dw_mmc/by-name/BOOT
systemlink=/dev/block/platform/dw_mmc/by-name/SYSTEM
known=cad5e76faf372beff3243ec615924286096713c994a6e5a03d3231f69478e6fd
old=f9292bab437bce12cea56e6e0c067a15c87475b69a61fc0c13fd4d3f07b9694f
die() { echo "G397 READ-ONLY BLOCK: $1" >&2; exit 1; }
echo "G397 SM-T311 preflight ONLY. No write/flash/reboot will be performed."
[ -x "$bb" ] || die "installed recovery BusyBox absent"
[ -x /sbin/twrp ] || die "not booted into expected TWRP recovery"
[ -L /sbin/sh ] || die "stock TWRP /sbin/sh symlink missing"
[ "$(id -u)" = "0" ] || die "recovery process not root"
[ -b "$boot" ] || die "physical BOOT p9 missing"
[ -b "$system" ] || die "physical SYSTEM p20 missing"
# Check EXACT aliases expected by this already-signed G397 updater-script,
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
echo "G397 verified BOOT block p9 exact expected prefix SHA."
# /data is unencrypted under current G379, but actual recovery mount and
# availability still require a real-world check (this is deliberately strict).
[ -f "$signed" ] || die "signed G397 ZIP not accessible through /data; STOP for safe owner sideload"
actualzip="$("$bb" sha256sum "$signed" | "$bb" cut -d' ' -f1)"
[ "$actualzip" = "$want" ] || die "signed G397 ZIP SHA differs; refuse"
echo "G397 signed G397 ZIP exact SHA PASS."
echo "G397 ALL READ-ONLY SAFETY GATES PASS. This did NOT install anything."
echo "MANUAL TWRP INSTALL STILL REQUIRES OWNER CONSENT AND CONTROL."
