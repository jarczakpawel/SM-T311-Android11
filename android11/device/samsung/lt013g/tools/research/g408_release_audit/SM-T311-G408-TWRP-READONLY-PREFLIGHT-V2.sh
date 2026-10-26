#!/sbin/sh
# SM-T311 G408 TWRP read-only preflight v2 for cumulative signed G408. (p20 sysfs identity across kernels)
# Never FLASH/reboot/mount/chcon/change SIM/modify any device or partition.
# To run ONLY AFTER manual hardware-key boot into TWRP:
# /sbin/sh /data/local/tmp/SM-T311-G408-TWRP-READONLY-PREFLIGHT.sh
# If /data cannot be mounted, STOP and arrange owner-supervised TWRP
# sideload/external SD instead. Never put a 484MB ZIP into TWRP tmpfs RAM.
set -eu
bb=/sbin/busybox
signed=/data/local/tmp/SM-T311-G408-GUARDED-ZRAM-GPS-SUPL-SIGNED.zip
want=5ae3595004b2043715408eed2e3f33f1ded920a3061f9f42504f0303e12c7a79
boot=/dev/block/mmcblk0p9
system=/dev/block/mmcblk0p20
bootlink=/dev/block/platform/dw_mmc/by-name/BOOT
systemlink=/dev/block/platform/dw_mmc/by-name/SYSTEM
known=cad5e76faf372beff3243ec615924286096713c994a6e5a03d3231f69478e6fd
old=f9292bab437bce12cea56e6e0c067a15c87475b69a61fc0c13fd4d3f07b9694f
die() { echo "G408 READ-ONLY BLOCK: $1" >&2; exit 1; }
echo "G408 SM-T311 preflight v2 ONLY. No write/flash/reboot will be performed."
[ -x "$bb" ] || die "installed recovery BusyBox absent"
[ -x /sbin/twrp ] || die "not booted into expected TWRP recovery"
[ -L /sbin/sh ] || die "stock TWRP /sbin/sh symlink missing"
[ "$(id -u)" = "0" ] || die "recovery process not root"
[ -b "$boot" ] || die "physical BOOT p9 missing"
[ -b "$system" ] || die "physical SYSTEM p20 missing"
# Check EXACT aliases expected by this already-signed G408 updater-script,
# NOT alternate Android /dev/block/platform/soc/... aliases.
[ -L "$bootlink" ] || die "TWRP old dw_mmc BOOT alias absent; signed guard would fail"
[ -L "$systemlink" ] || die "TWRP old dw_mmc SYSTEM alias absent; signed guard would fail"
[ "$("$bb" readlink -f "$bootlink")" = "$boot" ] || die "BOOT does not resolve to p9"
[ "$("$bb" readlink -f "$systemlink")" = "$system" ] || die "SYSTEM does not resolve to p20"
# block_image_update requires SYSTEM not to be mounted by recovery.
[ -r /sys/class/block/mmcblk0p20/dev ] || die "SYSTEM block major:minor unavailable"
[ -r /proc/self/mountinfo ] || die "recovery mountinfo unavailable"
system_dev="$("$bb" cat /sys/class/block/mmcblk0p20/dev)" || die "SYSTEM block identity read failed"
# Linux major:minor of the SAME p20 may differ between current Android
# (179:20) and the genuine older TWRP 3.1.1 kernel (259:4). Do not pin
# numeric allocation: bind identity to the verified physical p20 sysfs path.
case "$system_dev" in
    *:*) ;;
    *) die "SYSTEM sysfs major:minor absent" ;;
esac
echo "$system_dev" | "$bb" awk -F : 'NF==2 && $1~/^[0-9]+$/ && $2~/^[0-9]+$/ {valid=1} END{exit !valid}' ||
    die "SYSTEM sysfs major:minor format invalid"
[ "$("$bb" readlink -f "/sys/dev/block/$system_dev")" = \
  "$("$bb" readlink -f /sys/class/block/mmcblk0p20)" ] ||
    die "SYSTEM major:minor points at another block device"
mount_state="$("$bb" awk -v dev="$system_dev" '
    {
        if (NF < 10 || $1 !~ /^[0-9]+$/ || $2 !~ /^[0-9]+$/ ||
            $3 !~ /^[0-9]+:[0-9]+$/ ||
            substr($4, 1, 1) != "/" || substr($5, 1, 1) != "/")
            invalid=1
        separator=0
        for (i=7; i<=NF; ++i)
            if ($i == "-") {
                if (separator) invalid=1
                separator=i
            }
        if (separator < 7 || separator != NF - 3) invalid=1
        if ($3 == dev || $5 == "/system" || $5 == "/system_root") mounted=1
    }
    END {
        if (NR == 0 || invalid) print "INVALID"
        else if (mounted) print "MOUNTED"
        else print "CLEAR"
    }' /proc/self/mountinfo)" || die "SYSTEM mountinfo read failed"
[ "$mount_state" = "CLEAR" ] || die "SYSTEM mounted or mountinfo invalid; STOP"
# Only read the 6838272-byte BOOT prefix, exactly how the signed guard does.
actual="$("$bb" dd if="$boot" bs=2048 count=3339 2>/dev/null | "$bb" sha256sum | "$bb" cut -d' ' -f1)"
[ "$actual" = "$known" ] || [ "$actual" = "$old" ] ||
    die "unknown physical BOOT prefix. Refuse to continue"
echo "G408 verified BOOT block p9 exact expected prefix SHA."
# /data is unencrypted under current G379, but actual recovery mount and
# availability still require a real-world check (this is deliberately strict).
[ -f "$signed" ] || die "signed G408 ZIP not accessible through /data; STOP for safe owner sideload"
actualzip="$("$bb" sha256sum "$signed" | "$bb" cut -d' ' -f1)"
[ "$actualzip" = "$want" ] || die "signed G408 ZIP SHA differs; refuse"
echo "G408 signed G408 ZIP exact SHA PASS."
echo "G408 ALL READ-ONLY SAFETY GATES PASS. This did NOT install anything."
echo "MANUAL TWRP INSTALL STILL REQUIRES OWNER CONSENT AND CONTROL."
