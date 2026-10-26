#!/usr/bin/env bash
set -Eeuo pipefail
P=/home/debian/Projekty/samsung
A=/home/debian/android/lineage-18.1-sm-t311
IN=$P/out/SM-T311-G1749-MESA2625-GLPB-PROPERTY/src/gallium/targets/dri/libgallium_dri.so
DST=$A/device/samsung/lt01-common/mesa26-g816/experimental-g1749/libgallium_dri.so
[ ! -e "$DST" ] || exit 21
[ "$(sha256sum "$IN" | cut -d' ' -f1)" = fe8a15d701915f0178114d095555a2eb4d54dae95d5bbabd6f61a4d80639a3e0 ] || exit 22
[ "$(cat "$P/linux-7.3-port/SMT311_G1750_G1749_MESA2625_GLPB_PROPERTY_QA_20261026.json" | /usr/bin/grep -c G1750_G1749_ARM32_ELF_PROPERTY_OPTIN_SOURCE_AND_ABI_PASS)" = 1 ] || exit 23
bash "$A/device/samsung/lt01-common/mesa26-g816/stage.sh" --verify-staged | /usr/bin/grep -q G867_SIX_STAGED_ARM32_SHA256_PASS || exit 24
install -m 0644 "$IN" "$DST"
[ "$(sha256sum "$DST" | cut -d' ' -f1)" = fe8a15d701915f0178114d095555a2eb4d54dae95d5bbabd6f61a4d80639a3e0 ] || exit 25
printf 'G1752_ONE_OPTIN_GALLIUM_STAGED_PREBUILTS_UNCHANGED_NOT_FLASHED\n'
