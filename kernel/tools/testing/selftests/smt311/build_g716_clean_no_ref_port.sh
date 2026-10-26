#!/usr/bin/env bash
# G716 - only supported CLEAN no-ref port Linux 7.3 host builder for SM-T311.
# Never accesses tablet partitions, performs a recovery reboot or makes backups.
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../../.." && pwd)"
PROJECT="$(cd -- "$ROOT/.." && pwd)"
OUT="$PROJECT/build/g716-clean-refcounted"
DEFCONFIG="$ROOT/arch/arm/configs/sm_t311_g716_clean_defconfig"
cd "$ROOT"
export KBUILD_BUILD_TIMESTAMP='2026-10-21T00:00:00Z'
export KBUILD_BUILD_VERSION=1
export KBUILD_BUILD_USER=smt311
export KBUILD_BUILD_HOST=reproducible
test -f "$DEFCONFIG" || { echo 'STOP: missing tracked, clean G716 board defconfig'; exit 2; }
mkdir -p "$OUT"
if [ ! -f "$OUT/.config" ]; then
    make -s O="$OUT" ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- LOCALVERSION= sm_t311_g716_clean_defconfig
fi
make -s O="$OUT" ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- LOCALVERSION= olddefconfig
make -s O="$OUT" ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- LOCALVERSION= savedefconfig
cmp -s "$OUT/defconfig" "$DEFCONFIG" || {
    echo 'STOP: output config does not match tracked G716 CLEAN board defconfig'
    exit 2
}
python3 - "$OUT/.config" "$ROOT" <<'PY'
from pathlib import Path
import sys
cfg=Path(sys.argv[1]).read_text()
root=Path(sys.argv[2])
need=['CONFIG_ARCH_EXYNOS4=y','CONFIG_HIGHMEM=y','CONFIG_DEBUG_FS=y',
      'CONFIG_FUSE_FS=y','CONFIG_FUSE_PASSTHROUGH=y','CONFIG_LRU_GEN=y',
      'CONFIG_LOCALVERSION="-pjarczak"']
for x in need:
    if x not in cfg:
        raise SystemExit("STOP: missing unchanged hardware feature "+x)
for x in ['CONFIG_PAGE_RCU_FREE=y','CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y']:
    if x in cfg:
        raise SystemExit("STOP: abandoned port enabled "+x)
for x in ['PG_rcu_free','PAGE_RCU_FREE','SM_T311_RCU_HOT4K_EXPERIMENT']:
    if x in (root/'mm/filemap.c').read_text():
        raise SystemExit("STOP: port still present in mm/filemap.c")
PY
make -j6 O="$OUT" ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- LOCALVERSION= vmlinux zImage dtbs
test "$(cat "$OUT/include/config/kernel.release")" = "7.3.0-pjarczak"
python3 "$PROJECT/tools/g716_pack_clean_boot.py"
make -s -C tools/testing/selftests/smt311 check-clean-no-ref-port > "$OUT/g716-clean-release-check.json"
python3 - "$OUT/g716-clean-release-check.json" <<'PY'
from pathlib import Path
import json,sys
j=json.loads(Path(sys.argv[1]).read_text())
assert all(j['checks'].values()) and all(j['negative_controls'].values())
print("G716 CLEAN full ARM32 release gate PASSED",
      len(j['checks']),"positive",len(j['negative_controls']),"negative")
PY
echo "HOST ONLY / DO NOT FLASH / no G650 backup, no recovery reboot"
