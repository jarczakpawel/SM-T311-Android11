#!/system/bin/sh
# SM-T311 — G403 post-install noninvasive acceptance. No location/GPS activation.
# Run ONLY on booted Android11 AFTER owner-approved full G403 TWRP install:
# /system/bin/sh /data/local/tmp/SM-T311-G403-POSTINSTALL-READONLY.sh --read-only
# Does NOT flash/mount/write, invoke Network requests, change APN/SIM or reboot.
set -u
if [ "$#" -ne 1 ] || [ "$1" != "--read-only" ]; then
    echo "USAGE: invoke only with --read-only after approved FULL G403 ROM installation."
    exit 2
fi
want_gps=8cba224bb2f77a4d829b6ff7a4f1da1461357b0cad3eba9d704ae7e56e88db5b
gps_bin=/system/vendor/lib/hw/gps.default.so
vendor_fstab=/vendor/etc/fstab.lt013g
p11=/dev/block/mmcblk0p11
pass=0
fail=0
pending=0
report_pass() { printf 'PASS %s\n' "$1"; pass=$((pass+1)); }
report_fail() { printf 'FAIL %s\n' "$1"; fail=$((fail+1)); }
report_pending() { printf 'PENDING %s\n' "$1"; pending=$((pending+1)); }
echo "SM-T311 G403 POSTINSTALL READ-ONLY AUDIT — no device changes, no IPs or SIM details"
if [ "$(getprop ro.product.device)" = "lt013g" ]; then
    report_pass "REAL_SM_T311_MODEL"
else
    report_fail "NOT_LT013G_MODEL"
fi
if [ "$(getprop ro.build.version.release)" = "11" ] &&
   [ "$(getprop sys.boot_completed)" = "1" ]; then
    report_pass "ANDROID11_BOOT_COMPLETED"
else
    report_fail "ANDROID11_NOT_BOOTED"
fi
if [ "$(getenforce)" = "Enforcing" ]; then
    report_pass "GLOBAL_SELINUX_ENFORCING"
else
    report_fail "GLOBAL_SELINUX_NOT_ENFORCING"
fi
if [ -r "$gps_bin" ] &&
   [ "$(sha256sum "$gps_bin" 2>/dev/null | cut -d' ' -f1)" = "$want_gps" ]; then
    report_pass "EXACT_G403_MODERN_ARM32_GPS_ELF_INSTALLED"
else
    report_fail "G403_GPS_ELF_NOT_INSTALLED_OR_SHA_MISMATCH"
fi
# /misc should be declared for bootloader_message; this is NOT a partition mount.
if [ -r "$vendor_fstab" ] &&
   awk '$2=="/misc" && $3=="emmc" && $1 ~ /\/by-name\/OTA$/ {found=1}
        END {exit !found}' "$vendor_fstab"; then
    report_pass "SELECTED_VENDOR_FACTORY_OTA_P11_MISC_FSTAB"
else
    report_fail "SELECTED_VENDOR_MISC_FSTAB_MISSING"
fi
# Print ONLY the SELinux domain name in a PASS/FAIL code, never the block's content.
if [ -b "$p11" ] &&
   ls -Zd "$p11" 2>/dev/null | grep -q 'u:object_r:misc_block_device:s0'; then
    report_pass "ACTUAL_P11_MISC_BLOCK_SELINUX_LABEL"
else
    report_fail "ACTUAL_P11_SELINUX_LABEL_NOT_APPLIED"
fi
if [ "$(getprop init.svc.vendor.lt013g-gps-legacy-v195)" = "running" ]; then
    report_pass "LEGACY_BROADCOM_GPSD_SERVICE_RUNNING"
else
    report_pending "LEGACY_BROADCOM_GPSD_SERVICE_NOT_RUNNING"
fi
# Do not reveal the private PDP address, gateway, DNS, APN, subscriber ID or routing table.
# A /32 PDP host route must be installed before the "default via" route;
# the existence of routes alone DOES NOT establish Network VALIDATED.
if ip -4 -o addr show dev rmnet1 2>/dev/null |
   awk '$0 ~ / inet [0-9.]+\/32([[:space:]]|$)/ {found=1}
        END {exit !found}'; then
    report_pass "XMM6262_RMNET1_IPV4_32_ADDRESS_PRESENT"
else
    report_pending "CELLULAR_PDP_IPV4_32_NOT_PRESENT"
fi
route_data="$(ip -4 route show table all 2>/dev/null || :)"
gateway="$(printf '%s\n' "$route_data" |
   awk '$1=="default" && $2=="via" {
     for (i=1;i<=NF;i++) if ($i=="dev" && $(i+1)=="rmnet1") {
       print $3;exit
     }
   }')"
if [ -n "$gateway" ] &&
   printf '%s\n' "$route_data" |
   awk -v gw="$gateway" '$1==gw {
     for (i=1;i<=NF;i++) if ($i=="dev" && $(i+1)=="rmnet1") found=1
   } END {exit !found}'; then
    report_pass "PDP_32_GATEWAY_DIRECT_HOST_ROUTE_AND_DEFAULT"
else
    report_pending "PDP_32_GATEWAY_HOST_ROUTE_AND_DEFAULT_NOT_PROVEN"
fi
report_pending "CELLULAR_NETWORK_VALIDATED_REQUIRES_REAL_CONNECTIVITY_ACCEPTANCE"
report_pending "DNS_NTP_SUPL_AND_NATIVE_UTC_REQUIRE_REAL_POSTINSTALL_SESSION"
report_pending "OUTDOOR_STABLE_GPS_GLONASS_3D_REQUIRES_OWNER_TEST"
report_pending "RECOVERY_BCB_REBOOT_REQUIRES_SEPARATE_CONTROLLED_OWNER_TEST"
printf 'RESULT G403_READONLY_AUDIT PASS=%s FAIL=%s PENDING=%s\n' "$pass" "$fail" "$pending"
if [ "$fail" -ne 0 ]; then
    echo "STOP: required exact firmware/boot/SELinux parity not accepted."
    exit 1
fi
echo "SOURCE/BOOT PARITY PASS ONLY. PENDING measurements are NOT accepted."
exit 0
