#!/system/bin/sh
# G723 SM-T311 OTG read-only physical intake, usable over authenticated
# ADB Wi-Fi while a USB host adapter / pendrive is plugged in.
# NEVER requests USB mode/role, VBUS/BOOST, PHY reset or mounts a disk.
# Output excludes USB device identifiers and subscriber data.
set -eu
N="${1:-900}"
case "$N" in ''|*[!0-9]*) echo 'G723 seconds must be integer' >&2; exit 2;; esac
if [ "$N" -lt 1 ] || [ "$N" -gt 1800 ]; then
  echo 'G723 seconds out of 1..1800' >&2
  exit 2
fi
CUTOFF=$(($(date +%s) - N))
echo G723_OTG_PASSIVE=1
echo "KERNEL=$(uname -r)"
echo "DEVICE_BOOTED=$(getprop sys.boot_completed)"
echo "ADBD_USER_ID=$(id -u)"
echo "CHARGER_INPUT_ONLINE=$(cat /sys/class/power_supply/max77693-charger/online 2>/dev/null || echo UNAVAILABLE)"
echo "USB_GADGET_STATE=$(getprop sys.usb.state)"
echo "USB_GADGET_CONFIG=$(getprop sys.usb.config)"
echo "ANDROID_VOLD=$(getprop init.svc.vold)"
echo FRAMEWORK_USB_ROLE_BEGIN
dumpsys usb 2>/dev/null | grep -E 'current_functions=|current_functions_applied=|host_connected=|source_power=|sink_power=|kernel_state=' | head -n 8 || true
echo FRAMEWORK_USB_ROLE_END
ROLE_SYSFS=/sys/devices/platform/i2c-9/i2c-9/9-0066/max77693-charger/t311_usb_role_diag
MUIC_SYSFS=/sys/devices/platform/i2c-9/i2c-9/9-0066/max77693-muic/t311_muic_status
if [ -r "$ROLE_SYSFS" ]; then
  echo "KERNEL_USB_ROLE_DIAG=$(cat "$ROLE_SYSFS")"
else
  echo "KERNEL_USB_ROLE_DIAG=UNAVAILABLE_NOT_PROOF_HOST_OFF"
fi
if [ -r "$MUIC_SYSFS" ]; then
  echo "KERNEL_MUIC_DIAG=$(cat "$MUIC_SYSFS")"
else
  echo "KERNEL_MUIC_DIAG=UNAVAILABLE_NOT_PROOF_ID_OPEN"
fi
# SELinux enforcing shell cannot read all /sys USB idVendor/idProduct
# files; silently treating unreadable descriptors as 0 devices is WRONG.
USB_SYSFS_READABLE=0
USB_SYSFS_DENIED=0
for DEV in /sys/bus/usb/devices/*; do
  [ -e "$DEV" ] || continue
  [ -e "$DEV/idVendor" ] || continue
  if cat "$DEV/idVendor" >/dev/null 2>&1; then
    USB_SYSFS_READABLE=$((USB_SYSFS_READABLE + 1))
  else
    USB_SYSFS_DENIED=$((USB_SYSFS_DENIED + 1))
  fi
done
echo "USB_SYSFS_DESCRIPTOR_READABLE=$USB_SYSFS_READABLE"
echo "USB_SYSFS_DESCRIPTOR_DENIED=$USB_SYSFS_DENIED"
if [ "$USB_SYSFS_READABLE" -eq 0 ]; then
  echo "USB_SYSFS_DESCRIPTOR_VISIBILITY=RESTRICTED_OR_UNAVAILABLE"
fi
# Read the framework's UsbHostManager device inventory instead of
# guessing from the kernel's intentionally restricted sysfs. Its vendor_id
# and product_id are rendered as decimal; 5401:32 == modem 1519:0020.
dumpsys usb 2>/dev/null | awk '
  function finish() {
    if (!entry) return
    if (v==5401 && p==32) internal++
    else external++
    entry=0
  }
  /^[[:space:]]*name=\/dev\/bus\/usb\// {
    finish()
    entry=1; v=-1; p=-1
  }
  /^[[:space:]]*vendor_id=/ {
    if (entry && v<0) {z=$0; sub(/^.*vendor_id=/,"",z); v=z+0}
  }
  /^[[:space:]]*product_id=/ {
    if (entry && p<0) {z=$0; sub(/^.*product_id=/,"",z); p=z+0}
  }
  END {
    finish()
    print "ANDROID_USB_HOST_MANAGER_INTERNAL_MODEM=" internal+0
    print "ANDROID_USB_HOST_MANAGER_EXTERNAL_DEVICES=" external+0
  }'
USB_SCSI_BLOCK=0
for DEV in /sys/block/sd? /sys/block/sd??; do
  [ -d "$DEV" ] && USB_SCSI_BLOCK=$((USB_SCSI_BLOCK + 1))
done
echo "USB_SCSI_BLOCK_DEVICES=$USB_SCSI_BLOCK"
echo "ANDROID_STORAGE_DISKS_BEGIN"
sm list-disks 2>/dev/null || true
echo "ANDROID_STORAGE_DISKS_END"
echo "MAX77693_MUIC_EVENTS_LAST_${N}S_BEGIN"
logcat -d -b kernel -t 4000 -v epoch 2>/dev/null |
 awk -v cutoff="$CUTOFF" '
  $1 ~ /^[0-9]+\.[0-9]+$/ && ($1+0)>=cutoff &&
   /max77693-muic/ {
    if (/external connector is attached \(adc:0x00/) {ground_attach++;last="ID_GROUND_ATTACH"}
    else if (/external connector is detached \(adc:0x00/) {ground_detach++;last="ID_GROUND_DETACH"}
    else if (/failed/ || /error/) {muic_errors++}
   }
  END {
    print "ID_GROUND_ATTACH_EVENTS=" ground_attach+0
    print "ID_GROUND_DETACH_EVENTS=" ground_detach+0
    print "MUIC_ERROR_EVENTS=" muic_errors+0
    print "LAST_ID_EVENT=" (last==""?"NONE":last)
  }'
echo "MAX77693_MUIC_EVENTS_LAST_${N}S_END"
echo G723_OTG_PASSIVE_COMPLETE=1
