#!/system/bin/sh
# G725, SM-T311 USB PC DEVICE <-> OTG HOST eval coordinator.
# MANUAL root-only laboratory tool; no autorun, no GPS/modem operations.
# Requires G724 kernel, isolated laptop, established ADB Wi-Fi 5555.
# Do not arm 5V without a qualified VBUS/current measurement and rescue.
set -eu

CMD="${1:---status}"
TOKEN="${2:-}"
DWC2=/sys/bus/platform/drivers/dwc2
DWC2_ID=12480000.usb
GADGET=/config/usb_gadget/g1/UDC
POWER=/data/local/tmp/g724-otg-lab-supervisor-NOT-AUTORUN.sh
ROLE=/sys/devices/platform/i2c-9/i2c-9/9-0066/max77693-charger/t311_usb_role_diag
STOP=/sys/devices/platform/i2c-9/i2c-9/9-0066/max77693-charger/t311_otg_host_stop
INTERLOCK=/sys/devices/platform/i2c-9/i2c-9/9-0066/max77693-charger/t311_otg_interlock

fail() { echo "G725 REFUSE: $*" >&2; exit 3; }
role() { cat "$ROLE" 2>/dev/null || echo UNAVAILABLE; }
has() { case " $1 " in *" $2 "*) return 0 ;; *) return 1 ;; esac; }
usb_pc() { dumpsys usb 2>/dev/null | grep -m1 -E '^ *connected=' || true; }

case "$CMD" in
--status)
  echo "G725_USER_ID=$(id -u)"
  echo "G725_USB_PC=$(usb_pc)"
  echo "G725_GADGET_UDC=$(cat "$GADGET" 2>/dev/null || echo UNAVAILABLE)"
  echo "G725_DWC2_BOUND=$([ -L /sys/bus/platform/devices/$DWC2_ID/driver ] && echo 1 || echo 0)"
  echo "G725_PHY_ROLE=$(role)"
  echo "G725_POWER_INTERLOCK=$(cat "$INTERLOCK" 2>/dev/null || echo UNAVAILABLE)"
  echo "G725_ADB_TCP_STATE=$(ss -ltn 2>/dev/null | grep ':5555' | head -n 1 || echo UNAVAILABLE)"
  snap=$(role)
  lockstate=$(cat "$INTERLOCK" 2>/dev/null || echo UNKNOWN)
  if has "$snap" "extcon_host=1" && has "$snap" "extcon_usb=0" &&
     has "$lockstate" "safe_off=1" && has "$lockstate" "blocked=1"; then
    echo "G725_OTG_STATUS=HOST_PATH_READY_BUT_VBUS_LATCHED"
    echo "G725_OTG_NEXT=COMPLETE_PHYSICAL_ADAPTER_DETACH_AND_REINSERT"
  elif has "$snap" "extcon_host=1" && has "$snap" "extcon_usb=0" &&
       has "$lockstate" "safe_off=1" && has "$lockstate" "blocked=0"; then
    echo "G725_OTG_STATUS=HOST_DETECTED_BOOST_NOT_ENABLED"
  elif has "$snap" "extcon_usb=1"; then
    echo "G725_OTG_STATUS=PC_GADGET_PATH"
  else
    echo "G725_OTG_STATUS=UNKNOWN_OR_UNSAFE"
  fi
  exit 0 ;;
--host-lab-qualified) ;;
*)
  fail "usage: --status | --host-lab-qualified TOKEN" ;;
esac

[ "$TOKEN" = "G724-ELECTRICAL-AND-RECOVERY-QUALIFIED" ] ||
  fail "electrical and recovery qualifications not supplied"
[ "$(id -u)" = 0 ] || fail "root is required for host mode"
[ -r "$ROLE" ] && [ -e "$STOP" ] &&
  [ -r "$INTERLOCK" ] && [ -e "$DWC2/unbind" ] &&
  [ -e "$DWC2/bind" ] && [ -e "$GADGET" ] &&
  [ -r "$POWER" ] || fail "required G724 kernel/gadget hardware absent"

# The current tablet reports ADB TCP props -1 even when a real WiFi ADB
# listener and ESTABLISHED peer both exist.
for state in 0A 01; do
  cat /proc/net/tcp /proc/net/tcp6 2>/dev/null |
    awk -v state="$state" '$2 ~ /:15B3$/ && $4 == state { found=1 } END {exit !found}' ||
    fail "WiFi ADB 5555 socket state $state missing"
done
usb_pc | grep -q 'connected=true' &&
  fail "laptop remains electrically connected; do not source 5V"
# Fresh charger+MUIC preflight: cable plugged means HOST, not PC.
diag=$(role)
for check in extcon_usb=0 extcon_host=1 pmic_errno=0 boost_bits=0 vbus_present=0; do
  has "$diag" "$check" || fail "not a verified isolated OTG host: $check"
done
# G725 physically observed a sticky user_blocked=1 after PC -> HOST
# cable transition, with PMIC safe_off=1. Never tear down DWC2 or
# trigger a VBUS source operation while this safety latch is set.
# Only real OTG cable detach + verified OFF clears the latch in kernel.
safety=$(cat "$INTERLOCK" 2>/dev/null || echo UNKNOWN)
case " $safety " in
  *" shutdown=0 "*" armed=0 "*" blocked=0 "*" pmic_known=1 "*" pmic_boost=0 "*" safe_off=1 "*) ;;
  *) fail "PMIC safety interlock not clear; detach OTG adapter completely before retry" ;;
esac

# Android's DEVICE gadget cannot coexist with external PHY1 HOST lease:
# release DWC2/PHY0, then the PHY driver must publish MODE_SWITCH=HOST.
# MTP+USB adbd will stop during host mode; Wi-Fi adbd stays available.
RESTORE_DEVICE=0
cleanup() {
  # Do not rebind DEVICE/PC until VBUS has been verified OFF.
  if [ -e "$STOP" ]; then
    printf '1\n' > "$STOP" 2>/dev/null || true
  fi
  count=0
  while [ "$count" -lt 6 ]; do
    status=$(cat "$INTERLOCK" 2>/dev/null || echo UNKNOWN)
    case " $status " in
      *" safe_off=1 "*) break ;;
    esac
    sleep 1
    count=$((count+1))
  done
  case " $status " in
    *" safe_off=1 "*) ;;
    *)
      echo "G725 VBUS OFF UNVERIFIED: DEVICE PHY0 will remain unbound" >&2
      return 1 ;;
  esac
  if [ "$RESTORE_DEVICE" -eq 1 ]; then
    if [ ! -L "/sys/bus/platform/devices/$DWC2_ID/driver" ]; then
      printf '%s\n' "$DWC2_ID" > "$DWC2/bind" ||
        { echo "G725 DWC2 bind failed" >&2; return 1; }
    fi
    svc usb setFunctions mtp || true
    echo "G725 DEVICE USB RESTORE requested; reconnect laptop to verify"
  fi
}
trap 'cleanup || true' EXIT HUP INT TERM
RESTORE_DEVICE=1
svc usb setFunctions none ||
  fail "Android USB framework would not release gadget functions"
# Android init can own UDC: unbind it explicitly only once there is no PC.
printf '\n' > "$GADGET" || fail "gadget UDC unbind failed"
if [ -L "/sys/bus/platform/devices/$DWC2_ID/driver" ]; then
  printf '%s\n' "$DWC2_ID" > "$DWC2/unbind" || fail "DWC2 driver unbind failed"
fi
sleep 1
diag=$(role)
for check in extcon_usb=0 extcon_host=1 phy0_device_owners=0 mode_switch_host=1; do
  has "$diag" "$check" || fail "after PHY0 release USB HOST guard failed: $check"
done
# Interactive, watchdog-limited, NEVER daemonized. The kernel owns
# fail-closed PMIC OFF and monitors BYP current/thermal protection.
sh "$POWER" --run-lab-qualified "$TOKEN"
