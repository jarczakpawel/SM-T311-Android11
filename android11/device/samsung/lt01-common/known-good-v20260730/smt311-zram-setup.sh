#!/system/bin/sh
TAG=SMT311-ZRAM
ZSYS=/sys/block/zram0
ZDEV=/dev/block/zram0
SIZE=805306368

log -t "$TAG" "setup start" 2>/dev/null || true
[ -d "$ZSYS" ] || exit 0

grep -q "$ZDEV" /proc/swaps 2>/dev/null && {
    log -t "$TAG" "already active" 2>/dev/null || true
    exit 0
}

swapoff "$ZDEV" 2>/dev/null || true
[ -w "$ZSYS/reset" ] && echo 1 > "$ZSYS/reset"
[ -w "$ZSYS/comp_algorithm" ] && echo lz4 > "$ZSYS/comp_algorithm"
[ -w "$ZSYS/max_comp_streams" ] && echo 2 > "$ZSYS/max_comp_streams"
echo "$SIZE" > "$ZSYS/disksize" || exit 0

if /system/bin/toybox mkswap "$ZDEV" >/dev/null 2>&1; then
    /system/bin/toybox swapon -p 32758 "$ZDEV" >/dev/null 2>&1 || exit 0
elif command -v mkswap >/dev/null 2>&1; then
    mkswap "$ZDEV" >/dev/null 2>&1 || exit 0
    swapon -p 32758 "$ZDEV" >/dev/null 2>&1 || exit 0
else
    exit 0
fi

echo 100 > /proc/sys/vm/swappiness 2>/dev/null || true
echo 0 > /proc/sys/vm/page-cluster 2>/dev/null || true
log -t "$TAG" "active size=$SIZE algorithm=lz4" 2>/dev/null || true
exit 0
