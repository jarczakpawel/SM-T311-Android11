#!/usr/bin/env bash
# G700: HOST-ONLY SM-T311 RCU OFF kernel keeping CONFIG_DEBUG_FS.
# No adb / TWRP / backup / flash / recovery interaction.
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$ROOT"
EXP="$ROOT/../build/g686-reproducible-exp-on/.config"
OUT="$ROOT/../build/g700-debugfs-rcu-off"
FRAGMENT="$ROOT/arch/arm/configs/sm_t311_rcu_off_keep_debugfs_g700.fragment"
export KBUILD_BUILD_TIMESTAMP='2026-10-21T00:00:00Z'
export KBUILD_BUILD_VERSION=1
export KBUILD_BUILD_USER=smt311
export KBUILD_BUILD_HOST=reproducible
test -f "$EXP" && test -f "$FRAGMENT"
mkdir -p "$OUT"
# Use identical (G695 physically matched) Kconfig as experiment,
# removing ONLY RCU lifetime-unsafe option and PAGE_RCU_FREE.
python3 - "$EXP" "$FRAGMENT" "$OUT/.config" <<'PY'
from pathlib import Path
import sys
cfg=Path(sys.argv[1]).read_text()
f=Path(sys.argv[2]).read_text()
assert 'CONFIG_DEBUG_FS=y' in f
assert '# CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT is not set' in f
assert '# CONFIG_PAGE_RCU_FREE is not set' in f
assert cfg.count('CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y')==1
assert cfg.count('CONFIG_PAGE_RCU_FREE=y')==1
assert cfg.count('CONFIG_DEBUG_FS=y')==1
cfg=cfg.replace('CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y',
                '# CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT is not set',1)
cfg=cfg.replace('CONFIG_PAGE_RCU_FREE=y',
                '# CONFIG_PAGE_RCU_FREE is not set',1)
new=Path(sys.argv[3])
if new.exists():
    old=new.read_text()
    # Linux 7.3 olddefconfig elides '# CONFIG_PAGE_RCU_FREE is not set' on
    # 32-bit ARM because PAGE_RCU_FREE has def_bool 64BIT and no prompt.
    # Permit that EXACT normalized form, but no unrelated drift.
    normalized=cfg.replace('# CONFIG_PAGE_RCU_FREE is not set\n','')
    if old!=cfg and old!=normalized:
        raise SystemExit('G700 STOP: existing output profile different, refuse destructive reset')
else:
    new.write_text(cfg)
PY
make -s O="$OUT" ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- LOCALVERSION= olddefconfig
python3 - "$EXP" "$OUT/.config" <<'PY'
from pathlib import Path
import sys
def parse(p):
    d={}
    for line in Path(p).read_text().splitlines():
        if line.startswith('CONFIG_') and '=' in line:
            k,v=line.split('=',1);d[k]=v
        elif line.startswith('# CONFIG_') and line.endswith(' is not set'):
            d[line.split()[1]]='n'
    return d
a,b=map(parse,sys.argv[1:])
delta={k:(a.get(k),b.get(k)) for k in set(a)|set(b) if a.get(k)!=b.get(k)}
assert set(delta)=={'CONFIG_PAGE_RCU_FREE','CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT'},delta
assert b['CONFIG_DEBUG_FS']=='y' and b.get('CONFIG_PAGE_RCU_FREE')!='y'
assert b.get('CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT')=='n'
print('G700 KCONFIG PASS: only RCU no-ref experiment + PAGE_RCU_FREE disabled; DEBUG_FS retained')
PY
make -j6 O="$OUT" ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- LOCALVERSION= zImage dtbs vmlinux
test -s "$OUT/vmlinux" && test -s "$OUT/arch/arm/boot/zImage"
test -s "$OUT/arch/arm/boot/dts/samsung/exynos4212-tab3-3g8.dtb"
# A clean Git checkout must reproduce its own BOOT candidate BEFORE the
# binary regression, otherwise the regression would depend on stale host files.
# This is HOST filesystem-only and never creates a recovery installer.
python3 "$ROOT/../tools/g700_pack_debugfs_rcu_off.py"
python3 tools/testing/selftests/smt311/rcu_debugfs_off_binary_g700.py
echo 'G700 HOST ONLY, full ARM32 link verified; NO BOOT FLASH'
