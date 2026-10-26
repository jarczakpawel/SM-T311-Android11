#!/usr/bin/env python3
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

G1130 = "9090d2fa76cb3128542be2a2b3f9bf18ba8288cb"
ACCEPTED_CFG = "ca7d1c72025b012c34dca79ff65b234ab7e0b6b154bf04f7aaa4c06b0752c988"
ACCEPTED_DTB = "6fb3e77075733d531abaa2f4c3fa1bf55c11a50f04cc5acc9838db5ea0f41bfb"
MFC = {
    "drivers/media/platform/samsung/s5p-mfc/s5p_mfc.c":
        "28bfc47d0c77b2fb16d3f8a6528df8a36db7ef4424d3c5ee93c442b0e76af363",
    "drivers/media/platform/samsung/s5p-mfc/s5p_mfc_common.h":
        "41f04bc22374a1ba4b79f92f7ad07d60f646505d584e2bd0085a821c6dad9870",
    "drivers/media/platform/samsung/s5p-mfc/s5p_mfc_enc.c":
        "d2030d0651695c63fb8eb1c3f182e56366aa1e69d740d329fd86b976f05c547d",
}

def git(root, *args):
    return subprocess.run(["git", "-C", str(root), *args],
                          check=True, capture_output=True, text=True).stdout.strip()

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def verify(root, production, candidate):
    if subprocess.run(["git", "-C", str(root), "merge-base", "--is-ancestor",
                       G1130, "HEAD"]).returncode:
        raise RuntimeError("G1481_REFUSE_G958_ONLY_G1130_ABSENT")
    ref = root / "arch/arm/configs/sm_t311_android11_g1441_physical_defconfig"
    cfg = root / "arch/arm/configs/sm_t311_android11_defconfig"
    if sha(ref) != ACCEPTED_CFG or sha(cfg) != ACCEPTED_CFG:
        raise RuntimeError("G1481_REFUSE_PHYSICAL_CONFIG_MISMATCH")
    for name, expected in MFC.items():
        path = root / name
        if sha(path) != expected:
            raise RuntimeError("G1481_REFUSE_MFC_DRIVER_DRIFT_" + name)
        accepted = subprocess.check_output(
            ["git", "-C", str(root), "show", G1130 + ":" + name])
        if hashlib.sha256(accepted).hexdigest() != expected:
            raise RuntimeError("G1481_REFUSE_MFC_G1130_GIT_BLOB")
    for output, hz in ((production, "CONFIG_HZ=200"),
                       (candidate, "CONFIG_HZ=300")):
        if output:
            config = (output / ".config").read_text()
            if hz + "\n" not in config or "CONFIG_CHARGER_MAX77693_T311_OTG_EXPERIMENTAL=y\n" not in config:
                raise RuntimeError("G1481_REFUSE_OUTPUT_CONFIG_" + hz)
            dtb = output / "arch/arm/boot/dts/samsung/exynos4212-tab3-3g8.dtb"
            if sha(dtb) != ACCEPTED_DTB:
                raise RuntimeError("G1481_REFUSE_T311_3G_DTB")
            if b"G1130_MFC_RECORD_QOS" not in (output / "vmlinux").read_bytes():
                raise RuntimeError("G1481_REFUSE_MISSING_INSTALLED_MFC_QOS")
    return {
        "status": "G1481_ACCEPTED_PHYSICAL_G1130_MFC_SOURCE_PRESENT_PASS",
        "G1130_ancestor": G1130,
        "publishing_HEAD": git(root, "rev-parse", "HEAD"),
        "MFC_files_bit_exact": sorted(MFC),
        "physical_Kconfig_sha256": ACCEPTED_CFG,
        "physical_T311_3G_DTB_sha256": ACCEPTED_DTB,
        "production_200Hz_output_checked": bool(production),
        "isolated_300Hz_output_checked": bool(candidate),
        "experimental_300Hz_physically_installed": False,
        "new_boot_physically_installed": False,
        "bit_exact_zImage_rebuild_proven": False,
    }

if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--kernel", type=Path, default=Path(__file__).resolve().parents[2])
    p.add_argument("--production-output", type=Path)
    p.add_argument("--experiment-output", type=Path)
    args = p.parse_args()
    print(json.dumps(verify(args.kernel, args.production_output,
                            args.experiment_output), indent=2, sort_keys=True))
