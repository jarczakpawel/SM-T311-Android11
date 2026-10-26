#!/usr/bin/env python3
"""G1588: classify actual physical SM-T311 HWC DEVICE/CLIENT layer snapshots.

DEVICE = Exynos FIMD scanout plane, CLIENT = SurfaceFlinger GPU-composited
buffer. 'Composition efficiency' in the device's custom drm_hwcomposer is
1 - GPU *pixel-op estimates* / all estimated pixel operations. It is NOT
GPU utilization %, FPS, GPU bandwidth, a time %, nor CPU load.

No adb/graphical interactions; caller supplies optional source trees to
check the immutable FIMD WIN2/win3 transparency compatibility guard.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path

DIR = Path(__file__).resolve().parent / "testdata"
FILES = {name: DIR / ("g1587-physical-fimd-hwc-" + name.lower() + ".txt")
         for name in ("HOME", "OPEN", "CLOSE", "SUMMARY")}
EXPECTED = {
    "HOME": "63cc2930a6377aa6231df6c9cb34ba50b79cea74dabf6db0617501713fca08dd",
    "OPEN": "b97bc8bfefadf12471ecbca42b05468ded27faede5ec33b2f2676744ebc9ee63",
    "CLOSE": "928cee9444ac2e54603a87a521de9916dad213958b1764127c32c88aa1f3a204",
    "SUMMARY": "827b29f260e515c826382d58b393230ba11cc9213036b7f5238ede3082934a38",
}

def parse(base=DIR, check_source=False, android=None, kernel=None):
    samples = {}
    for name in ("HOME", "OPEN", "CLOSE"):
        file = base / FILES[name].name
        raw = file.read_bytes()
        digest = hashlib.sha256(raw).hexdigest()
        if digest != EXPECTED[name]:
            raise ValueError("G1588_REFUSE_CHANGED_PHYSICAL_HWC_" + name)
        lines = raw.decode("utf-8").splitlines()
        layers = []
        for line in lines:
            match = re.search(r"Output Layer 0x[0-9a-fA-F]+\((.*?)\)$", line)
            if match:
                layers.append({"name": match.group(1)})
                continue
            match = re.search(r"hwc: layer=0x[0-9a-fA-F]+ composition=(DEVICE|CLIENT) \((\d+)\)", line)
            if match:
                if not layers or "composition" in layers[-1]:
                    raise ValueError("G1588_MISMATCHED_LAYER_OUTPUT_" + name)
                comp = match.group(1)
                if (comp == "DEVICE") != (match.group(2) == "2"):
                    raise ValueError("G1588_COMPOSITION_ENUM_MISMATCH")
                layers[-1]["composition"] = comp
        if not layers or any("composition" not in l for l in layers):
            raise ValueError("G1588_MISSING_OR_UNMATCHED_HWC_LAYERS_" + name)
        def ints(phrase):
            return [int(v) for v in re.findall(re.escape(phrase)+r"\s*(\d+)", raw.decode("utf-8"))]
        def ratios():
            return [float(v) for v in re.findall(r"Composition efficiency:\s*([0-9.]+)", raw.decode("utf-8"))]
        hmiss=ints("HWC missed frame count:")
        total=ints("Total frames count:")
        unexpected=ints("ValidateDisplay unexpected composition failures:")
        present=ints("PresentDisplay composition errors (NOT atomic-only):")
        atomic=ints("Actual DRM atomic frame ioctl failures (compositor):")
        efficiency=ratios()
        if len(hmiss)!=1 or len(total)!=2 or len(efficiency)!=2 or            len(unexpected)!=2 or len(present)!=2 or len(atomic)!=1:
            raise ValueError("G1588_INCOMPLETE_HWC_PERFORMANCE_COUNTERS_" + name)
        if any(unexpected) or any(present) or any(atomic):
            raise ValueError("G1588_RECENT_HWC_ERROR_NOT_ZERO_" + name)
        samples[name] = {
            "source_sha256": digest,
            "layers": layers,
            "device_layers": sum(l["composition"]=="DEVICE" for l in layers),
            "client_layers": sum(l["composition"]=="CLIENT" for l in layers),
            "cumulative_frame_count": total[0],
            "last_dumpsys_interval_frame_count": total[1],
            "cumulative_hwc_missed_frames": hmiss[0],
            "cumulative_estimated_pixel_offload_fraction": efficiency[0],
            "last_dumpsys_estimated_pixel_offload_fraction": efficiency[1],
            "composition_errors": 0,
        }
    summary = base / FILES["SUMMARY"].name
    raw = summary.read_bytes()
    if hashlib.sha256(raw).hexdigest() != EXPECTED["SUMMARY"]:
        raise ValueError("G1588_REFUSE_CHANGED_HWC_SUMMARY")
    state = dict(line.split("=",1) for line in raw.decode("utf-8").splitlines())
    if {key: state.get(key) for key in
        ("device", "rom_build", "kernel", "selinux", "hwmixed",
         "singleatomic", "composer_service", "lmkd")} != {
        "device": "lt013g", "rom_build": "SMT311G1441",
        "kernel": "7.3.0-pjarczak", "selinux": "Enforcing",
        "hwmixed": "1", "singleatomic": "1", "composer_service": "running",
        "lmkd": "running",
    }:
        raise ValueError("G1588_PHYSICAL_PROFILE_NOT_ACCEPTED")
    checks = {}
    if check_source:
        if not android or not kernel:
            raise ValueError("G1588_REQUIRE_BOTH_ANDROID_KERNEL_SOURCE_ROOTS")
        hc=(android/"external/drm_hwcomposer/drmhwctwo.cpp").read_text()
        hp=(android/"external/drm_hwcomposer/platform/platform.cpp").read_text()
        kp=(kernel/"drivers/gpu/drm/exynos/exynos_drm_fimd.c").read_text()
        kplane=(kernel/"drivers/gpu/drm/exynos/exynos_drm_plane.c").read_text()
        source_cases={
            "kernel_fimd_primary_win2": "ctx->primary_win = tab3 ? 2 : DEFAULT_WIN;" in kp,
            "kernel_fimd_immutable_zpos": "!(config->capabilities & EXYNOS_DRM_PLANE_CAP_ZPOS)" in kplane,
            "kernel_w2_alpha_opaque_source": "win == ctx->primary_win" in kp and
                                               "pixel_format = DRM_FORMAT_XRGB8888" in kp,
            "kernel_fimd_other_windows_alpha": "EXYNOS_DRM_PLANE_CAP_WIN_BLEND | EXYNOS_DRM_PLANE_CAP_PIX_BLEND" in kp,
            "hwc_client_contiguous_fallback": "client_start = 0;" in hc and
                                               "client_size = z_map.size();" in hc,
            "hwc_experiment_mixed_primary": 'persist.vendor.t311.hwc_mixed_primary' in hc,
            "hwc_known_win2_win3": "WIN2" in hc and "WIN3" in hc,
            "planner_primary_before_overlay": hp.index("primary_planes->begin()") < hp.index("overlay_planes->begin()"),
            "planner_checks_immutable_z": "plane->zpos_property().is_immutable()" in (
                android/"external/drm_hwcomposer/include/platform.h").read_text(),
        }
        if not all(source_cases.values()):
            raise ValueError("G1588_PHYSICAL_FIMD_HWC_SOURCE_CONTRACT_FAILED " +
                             str([k for k,v in source_cases.items() if not v]))
        checks=source_cases
    return {
        "schema": "sm-t311-g1588-physical-exynos-fimd-window-overlay-offload-v1",
        "on_device": state, "states": samples,
        "reproduced_source_contract": checks,
        "notes": [
            "All three snapshots were captured on the same *physical* SM-T311.",
            "DRM HWC full-screen foreground application and notification windows may appear or disappear depending on visibility and timing.",
            "CLIENT means the final BUFFER COMPOSITION is performed by SurfaceFlinger using GPU; each app can still render its own buffer using GPU or CPU.",
            "DEVICE means direct HWC hardware plane composition; not proof that Mali GPU is idle.",
            "Custom HWC composition efficiency is estimated pixel-operation fraction offloaded, NOT GPU utilization or the janky-frame fraction.",
            "FIMD window 2 has stock-compatible opaque-only scanout. Its zpos is immutable, and WIN3 supports blend. Making WIN2 transparent or arbitrary plane reordering can obscure the UI.",
            "Existing property persist.vendor.t311.hwc_mixed_primary=1 is ALREADY ON. Do not claim a brand new toggle is needed.",
            "This is a read-only diagnostic with no runtime optimization or FPS claim."
        ],
    }

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--android-root",type=Path)
    ap.add_argument("--kernel-root",type=Path)
    ap.add_argument("--output",type=Path)
    args=ap.parse_args()
    if bool(args.android_root) != bool(args.kernel_root):
        ap.error("both source root arguments must be provided together")
    data=parse(check_source=bool(args.android_root),android=args.android_root,kernel=args.kernel_root)
    out=json.dumps(data,sort_keys=True,indent=2)+"\n"
    if args.output:
        if args.output.exists() or args.output.is_symlink():
            ap.error("G1588_NO_OVERWRITE_IMMUTABLE_PHYSICAL_REPORT")
        args.output.write_text(out)
    print(out)

if __name__=="__main__":
    main()
