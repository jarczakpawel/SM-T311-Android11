#!/system/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Samsung SM-T311 Linux 7.3 RCU/fuse fail-closed read-only physical health probe.
# It never sets RCU=1, writes block devices, tests audio/camera, or reboots.
set -eu

BOOT_EXPECT=dc0759441e457b31e1808c822acf21e4d76352e9bc4512158e71df6e0f3efd1f
REL_EXPECT=7.3.0-pjarczak
fail() { echo "G637 FAIL: $*" >&2; exit 1; }
info() { echo "G637 $*"; }

[ "$(id -u)" = "0" ] || fail "requires root ADB"
[ "$(uname -r)" = "$REL_EXPECT" ] || fail "unexpected kernel release"
[ "$(getprop sys.boot_completed)" = "1" ] || fail "Android boot incomplete"
[ "$(getenforce)" = "Enforcing" ] || fail "unexpected SELinux state"
[ "$(getprop init.svc.ril-daemon)" = "running" ] || fail "radio daemon not running"

debug_options=$(awk '$2=="/sys/kernel/debug" && $3=="debugfs" {print $4}' /proc/mounts)
if [ -n "$debug_options" ]; then
    case "$debug_options" in
        ro|ro,*) : ;;
        *) fail "existing debugfs mount is NOT readonly; refuse diagnostic" ;;
    esac
else
    mount -t debugfs -o ro debugfs /sys/kernel/debug ||
        fail "unable to mount readonly debugfs"
fi

[ -r /sys/kernel/debug/lockless_page_cache_read ] ||
    fail "RCU status missing from readonly debugfs"
rcu=$(cat /sys/kernel/debug/lockless_page_cache_read)
[ "$rcu" = "0" ] || fail "RCU no-ref path is ON; unattended test forbidden"

boot=$(sha256sum /dev/block/mmcblk0p9 | awk '{print $1}')
[ "$boot" = "$BOOT_EXPECT" ] || fail "BOOT sha256 mismatch"
[ "$(getprop persist.sys.fuse.passthrough.enable)" = "true" ] ||
    fail "Android MediaProvider passthrough property not enabled"
awk '$2=="/data" && $3=="ext4" {found=1} END {exit !found}' /proc/mounts ||
    fail "expected /data ext4 missing"
awk '$2=="/storage/emulated" && $3=="fuse" {found=1} END {exit !found}' /proc/mounts ||
    fail "expected MediaProvider emulated FUSE mount missing"

info "release=$(uname -r) boot_completed=1 selinux=Enforcing rcu=OFF"
info "BOOT_SHA256=$boot"
info "debugfs=readonly"
info "fuse_property=true; negotiated passthrough status NOT established"
awk '$1=="nr_free_rcu" || $1=="pgfree_rcu" || $1=="pgfree_rcu_mark" || $1=="pgfree_rcu_drain" || $1=="pgfree_rcu_expedite" {
    print "G637 vmstat_" $1 "=" $2
}' /proc/vmstat
awk '
/^Node [0-9]+, zone/ {
    zone=$4
}
/^[[:space:]]*pages free/ {
    if (zone=="Normal" || zone=="HighMem") free[zone]=$3
}
/^[[:space:]]*high[[:space:]]/ {
    if (zone=="Normal" || zone=="HighMem") hi[zone]=$2
}
/^[[:space:]]*nr_free_rcu/ {
    if (zone=="Normal" || zone=="HighMem") deferred[zone]=$2
}
END {
    for (i=1; i<=2; i++) {
        z=(i==1?"Normal":"HighMem")
        if (!(z in free) || !(z in hi) || !(z in deferred)) exit 2
        print "G637 zone=" z " free_pages=" free[z] " high_wmark=" hi[z] " deferred_rcu_pages=" deferred[z]
    }
}
' /proc/zoneinfo || fail "zone statistics unavailable"

for c in /sys/fs/fuse/connections/*; do
    [ -d "$c" ] || continue
    conn=${c##*/}
    wait=$(cat "$c/waiting")
    bg=$(cat "$c/max_background")
    info "fuse_connection=$conn waiting=$wait max_background=$bg"
done
info "PASS: read-only health check; no RCU activation, no flash"
