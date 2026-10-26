#!/system/bin/sh
# CM14 T311 80cfw behavior adapted from init.d to a normal Android 11 service.
mkdir -p /data/cfw
found=0
for f in /vendor/firmware/SlimISP_*.bin /system/vendor/firmware/SlimISP_*.bin; do
    [ -f "$f" ] || continue
    cp -f "$f" /data/cfw/ 2>/dev/null && found=1
done
if [ "$found" = 1 ]; then
    chown system:media /data/cfw/* 2>/dev/null || true
    chmod 0775 /data/cfw/* 2>/dev/null || true
    restorecon_recursive /data/cfw 2>/dev/null || true
    setprop t311.camera.fw_state ready
else
    setprop t311.camera.fw_state no_slimisp_blob
fi
exit 0
