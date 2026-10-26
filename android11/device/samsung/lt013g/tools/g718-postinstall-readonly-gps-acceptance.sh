#!/system/bin/sh
# G718: on-device READ-ONLY verification of the physically installed GPS G717.
# Does not reboot, remount, write GNSS parameters, disclose coordinates,
# activate a new GPS session, touch modem/PIN, or create backups.
set -eu
fail() { echo "G718 GPS PHYSICAL ACCEPTANCE FAIL: $1"; exit 1; }
NEW=c85b7b9e28f6bc8ac35d94d9b56011efde66b7d555dab1e7f7e3a5d90e7215a8
BOOT=2d477816fef0dbe2f41ca30747201dbe773e3dc3e710348c118226ee2999b7a3
RECOVERY=f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f
test "$(uname -r)" = "7.3.0-pjarczak" || fail "unexpected kernel release"
test "$(getprop ro.product.device)" = "lt013g" || fail "not SM-T311"
test "$(getprop sys.boot_completed)" = "1" || fail "Android not completely booted"
test "$(getenforce)" = "Enforcing" || fail "SELinux state drift"
test "$(sha256sum /vendor/lib/hw/gps.default.so | awk '{print $1}')" = "$NEW" ||
    fail "installed GPS HAL SHA does not match G717 ARM32 build"
test "$(stat -c '%u:%g:%a' /vendor/lib/hw/gps.default.so)" = "0:0:644" ||
    fail "GPS HAL ownership/mode changed"
test "$(ls -Z /vendor/lib/hw/gps.default.so | grep -o 'u:object_r:vendor_file:s0' | head -n 1)" = "u:object_r:vendor_file:s0" ||
    fail "GPS HAL SELinux label changed"
awk '$5=="/" && $6 ~ /^ro(,|$)/ { ok=1 } END { exit !ok }' /proc/self/mountinfo ||
    fail "SYSTEM was not remounted read-only"
test "$(getprop init.svc.vendor.gnss_service)" = "running" ||
    fail "GNSS HIDL service not running"
test "$(getprop init.svc.vendor.lt013g-gps-legacy-v195)" = "running" ||
    fail "legacy bridge not running"
echo "G718 GPS HAL installed SHA, 0644, vendor_file, Enforcing, SYSTEM read-only, both services: PASS"
if [ "$(id -u)" = 0 ]; then
    test "$(sha256sum /dev/block/mmcblk0p9 | awk '{print $1}')" = "$BOOT" ||
        fail "physical BOOT SHA changed"
    test "$(sha256sum /dev/block/mmcblk0p10 | awk '{print $1}')" = "$RECOVERY" ||
        fail "physical RECOVERY SHA changed"
    echo "G718 BOOT p9 and RECOVERY p10 immutable SHA: PASS"
else
    echo "G718 BOOT/RECOVERY SHA: NOT CHECKED (read-only shell permissions); root verification done during G718 install"
fi
echo "G718 PHYSICAL GPS INSTALLATION ACCEPTANCE PASS (not a GPS location fix)"
