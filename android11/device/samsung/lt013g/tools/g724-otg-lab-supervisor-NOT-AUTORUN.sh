#!/system/bin/sh
# SM-T311 G724 OTG TEST SUPERVISOR. EXPERIMENTAL, NOT AUTO-INSTALLED.
#
# Default: read-only status (works safely on current G650/G716 kernels).
# --run-lab-qualified requires a deliberately explicit engineering token,
# ROOT, physically detached PC, live authenticated Wi-Fi ADB, and G724's
# kernel fail-closed PMIC/PHY gates. Never enables BOOST on boot or attach.
# A successful sysfs ACK is not evidence of VBUS: verify PMIC readback.
# Under permanent use this needs qualified electrical testing and Android
# role/lifecycle integration; DO NOT DEPLOY as a production service.
set -eu
MODE="${1:---status}"
TOKEN="${2:-}"
SYSROOT=/sys/devices
ROLE=$(find "$SYSROOT" -name t311_usb_role_diag -type f -print 2>/dev/null | head -n 1 || true)
if [ -z "$ROLE" ]; then
  echo "G724_KERNEL_ROLE_DIAG=UNAVAILABLE"
  echo "G724_PRODUCTION_G650_OR_DRIVER_UNBOUND_NO_OTG_BOOST=1"
  [ "$MODE" = "--status" ] && exit 0
  echo "REFUSE: no G724 role gate is present" >&2
  exit 3
fi
BASE="${ROLE%/*}"
INTERLOCK="$BASE/t311_otg_interlock"
REFRESH="$BASE/t311_otg_host_refresh"
STOP="$BASE/t311_otg_host_stop"
value() {
  printf '%s\n' "$1" | tr ' ' '\n' | sed -n "s/^$2=//p" | head -n 1
}
diag() {
  cat "$ROLE" 2>/dev/null || true
}
lock() {
  cat "$INTERLOCK" 2>/dev/null || true
}
SAFE=$(diag)
echo "G724_USB_ROLE_SNAPSHOT=$SAFE"
echo "G724_USB_INTERLOCK_SNAPSHOT=$(lock)"
echo "G724_PC_USB_FRAMEWORK_STATE=$(dumpsys usb 2>/dev/null | grep -m1 -E '^ *connected=' | tr -d ' ' || true)"
if [ "$MODE" = "--status" ]; then
  exit 0
fi
if [ "$MODE" != "--run-lab-qualified" ] ||
   [ "$TOKEN" != "G724-ELECTRICAL-AND-RECOVERY-QUALIFIED" ]; then
  echo "REFUSE: supervised electrical/recovery qualification not acknowledged" >&2
  exit 3
fi
[ "$(id -u)" = "0" ] || { echo "REFUSE: requires privileged engineering shell" >&2;exit 3; }
for f in "$INTERLOCK" "$REFRESH" "$STOP"; do
  [ -e "$f" ] || { echo "REFUSE: G724 sysfs interface incomplete" >&2;exit 3; }
done
# Do not ever source VBUS into a laptop or a power/charger-connected port.
# Wi-Fi ADB is mandatory, USB-PC ADB must NOT be the only rescue transport.
# Both service.adb.tcp.port and persist.adb.tcp.port can report -1 on
# this Android11 build while adbd is *actually* listening on TCP/5555.
# Check the kernel socket table, plus an ESTABLISHED Wi-Fi peer instead.
if ! cat /proc/net/tcp /proc/net/tcp6 2>/dev/null |
     awk '$2 ~ /:15B3$/ && $4 == "0A" {found=1} END {exit !found}'; then
  echo "REFUSE: no actual ADB TCP 5555 listening socket" >&2
  exit 3
fi
if ! cat /proc/net/tcp /proc/net/tcp6 2>/dev/null |
     awk '$2 ~ /:15B3$/ && $4 == "01" {found=1} END {exit !found}'; then
  echo "REFUSE: no established Wi-Fi ADB rescue connection" >&2
  exit 3
fi
if dumpsys usb 2>/dev/null | grep -q -E '^ *connected=true'; then
  echo "REFUSE: Android still connected to laptop/USB gadget" >&2
  exit 3
fi
# No physical source operation when any critical state is unknown.
preflight() {
  EXPECT_BOOST="$1"
  d=$(diag)
  for needed in "extcon_host=1" "extcon_usb=0" "pmic_errno=0" \
       "boost_bits_known=1" "boost_bits=$EXPECT_BOOST" "muic_errno=0" \
       "vbus_present_known=1" "phy_errno=0" \
       "phy0_device_owners=0" "mode_switch_known=1" "mode_switch_host=1" \
       "no_power_control=0" "otg_ilim_known=1" "otg_ilim_unqualified=0"; do
    case " $d " in *" $needed "*) ;; *)
       echo "G724 REFUSE: missing safety condition $needed" >&2
       return 1;;
    esac
  done
  hcd=$(value "$d" host_hcd_ready_flags)
  case "$hcd" in 0x*|[0-9]*) ;; *) echo "REFUSE: unreadable HCD flags" >&2;return 1;; esac
  # EHCI=BIT(1)=0x2 and OHCI=BIT(2)=0x4 in Samsung USB2 PHY.
  [ $((hcd & 6)) -eq 6 ] ||
    { echo "REFUSE: EHCI+OHCI companion HCDs not both ready" >&2;return 1; }
  runtime=$(value "$d" host_runtime_flags)
  case "$runtime" in 0x*|[0-9]*) ;; *) echo "REFUSE: unknown EHCI runtime state" >&2;return 1;; esac
  [ $((runtime & 2)) -eq 2 ] ||
    { echo "REFUSE: EHCI host runtime owner not active" >&2;return 1; }
  lease=$(value "$d" phy1_host_owners)
  case "$lease" in [1-9]* ) ;; * )
     echo "REFUSE: no PHY1 host owner" >&2;return 1;;
  esac
  return 0
}
# Do not let an earlier charger/USB transition or BYP error latch
# cause DWC2 teardown or an impossible refresh that only returns EBUSY.
status=$(lock)
case " $status " in
  *" shutdown=0 "*" blocked=0 "*" pmic_known=1 "*" pmic_boost=0 "*" safe_off=1 "*) ;;
  *) echo "REFUSE: sticky OTG interlock; physical detach required" >&2; exit 3;;
esac
preflight 0 || exit 3
[ "$(value "$(diag)" vbus_present)" = 0 ] ||
  { echo "REFUSE: external VBUS sensed before host source" >&2;exit 3; }
# Kernel 3s watchdog always revokes BOOST without ongoing valid refresh.
# On explicit stop, kernel verifies BOOST OFF; do not infer OFF from sysfs ACK.
stop() {
  echo 1 > "$STOP" 2>/dev/null || true
  n=0
  while [ "$n" -lt 5 ]; do
    s=$(lock)
    case " $s " in *" safe_off=1 "*) echo "G724_VERIFIED_OFF=1";return 0;; esac
    sleep 1
    n=$((n + 1))
  done
  echo "G724_OFF_NOT_VERIFIED=1; kernel watchdog/PMIC access must be checked" >&2
  return 1
}
trap 'stop || true' EXIT HUP INT TERM
echo "G724_EXPERIMENTAL_ARMING_ONCE=1"
echo 1 > "$REFRESH"
sleep 1
while :; do
  preflight 1 || break
  state=$(lock)
  case " $state " in
    *" pmic_known=1 "*" pmic_boost=1 "*)
       echo "G724_PMIC_5V_SOURCE_REGISTER_BITS_ACTIVE=1" ;;
    *)
       echo "G724_PMIC_BOOST_NOT_PROVEN; stopping" >&2
       break;;
  esac
  echo 1 > "$REFRESH" || break
  sleep 1
done
echo "G724_FAIL_CLOSED_TERMINATION=1"
exit 2
