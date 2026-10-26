# G1588 — SM-T311 real Exynos FIMD hardware overlay offload (Android11 Linux7.3)

Date 2026-10-25. **Measured, source-tested; no graphics binary/kernel patch or physical flash.** Current actual tablet: SM-T311 lt013g Android11 installed G1568 SystemUI, kernel7.3.0-pjarczak G1130 BOOT, Mesa26.2.5 Lima, TWRP unchanged, Enforcing, LMKD running, working GPS/RIL/camera/Codec2/audio/USB untouched. The user requested explanation and investigation of HWC hardware planes and why some graphics use GPU.

## Distinguish GPU from HWC scanout

- An Android application can draw its own buffer with OpenGL ES/Mali400. HWC then decides whether FIMD directly scans out/composites that *already rendered* buffer as a `DEVICE` hardware plane or asks SurfaceFlinger to GLES-composite multiple buffers into a `CLIENT` target. DEVICE does not imply app drawing avoided GPU, and CLIENT does not imply CPU rasterizing the whole screen. Offloading supported composition to FIMD generally saves GPU work/memory bandwidth; CPU savings are incidental, not 1:1.
- `drm_hwcomposer` **Composition efficiency** is source-defined as `1 - gpu_pixops / total_pixops`, summing estimated rectangular pixel operations across frames; it is not GPU % usage, app FPS, or CPU/GPU time. Cumulative ~0.40 means ~40% estimated HWC pixel operations saved, not 40% of GPU busy cycles. Different scene content/geometry changes the estimated ratio.
- Official AOSP Hardware Composer docs: https://source.android.com/docs/core/graphics/hwc and https://source.android.com/docs/core/graphics/implement-hwc.

## Three READ-ONLY actual on-device HWC states

`tools/g1587_silent_fimd_hwc_layer_snapshot.sh`, source Git tracked, was physically USB pushed and executed on the awake, boot-complete tablet; without shell root, properties, toggles, touch sounds, service restart, recovery, ADB root, backups, wipe, firmware, or new binaries. It captured a HOME baseline, an OPEN post-swipe state, and a CLOSE state, then closed the shade and left device alone. Four exact sparse HWC output files are included in this device Git tree at `tools/testdata/g1587-physical-fimd-hwc-{home,open,close,summary}.txt`.

| HWC sample | Visible SurfaceFlinger layers | `DEVICE` hardware plane | `CLIENT` GPU composition | HWC estimated offload fraction **for preceding dumpsys interval**, not utilization |
|---|---:|---:|---:|---:|
| HOME | 4 | 1 (wallpaper) | 3 (launcher, StatusBar, NotificationShade) | 0.330579 |
| OPEN | 3 | 1 (StatusBar) | 2 (wallpaper, launcher) | 0.259633 |
| CLOSE | 4 | 1 (wallpaper) | 3 (launcher, StatusBar, NotificationShade) | 0.320748 |

These are snapshots AFTER touch gestures, NOT a synchronized every-vsync or every-frame record; the shade layer was not present in the OPEN snapshot while StatusBar was DEVICE. Do not conclude these are all frames during open animation. In all three physical HWC snapshots, validate failures=0, PresentDisplay errors=0, DRM atomic errors=0, and `persist.vendor.t311.hwc_mixed_primary=1`, `persist.vendor.t311.hwc_single_atomic=1` were already on. HWC missed frame count=209 cumulative, *not* per-capture missed-frame count. Most importantly, hardware overlay **IS ACTUALLY USED**; this is not a GPU-only compositor configuration.

### Why hardware cannot trivially show all layers

- Physical kernel source `drivers/gpu/drm/exynos/exynos_drm_fimd.c` selects **PRIMARY physical window WIN2** on Tab3 (`ctx->primary_win = tab3 ? 2 : DEFAULT_WIN;`), hard-coded zpos equal to physical index; `exynos_drm_plane.c` exposes immutable zpos for these FIMD windows without CAP_ZPOS. WIN0 lacks alpha; WIN1/WIN3 have win+pixel blending capabilities; WIN4 is the DRM cursor plane by default.
- FIMD stock-compatible path **forces WIN2 ARGB/ABGR to opaque XRGB/XBGR** even if KMS atomic TEST succeeds. The kernel and Android HWC already contain per-device defensive guards rejecting translucent PRIMARY WIN2, preserving an opaque client bottom and premultiplied WIN3 overlay when safe. Turning off these guards can cause visually occluded layers, blank UI or black backgrounds, even if DRM TEST_ONLY passes.
- Actual HWC `external/drm_hwcomposer/drmhwctwo.cpp` includes a protected-content guard, mixed primary/overlay planner, contiguous GPU CLIENT grouping, and one-atomic-Present. The experimental mixed-mode property is **already 1**. HWC planner `external/drm_hwcomposer/platform/platform.cpp` currently prioritizes PRIMARY WIN2 before overlays; with immutable z ordering this limits use of lower WIN0/WIN1 underneath WIN2. Arbitrarily reordering planes or promoting all to DEVICE is unsafe. Potential next source-level target: opt-in z-aware planner admitting low-z WIN1 for a confirmed opaque, correctly formatted lower layer **only if** the follow-on WIN2 layer is truly opaque and top layer fits WIN3 alpha format; validate on real FIMD glass and DRM atomic TEST with fallbacks. WIN4 as regular overlay also requires a dedicated capability test. These are **proposed**, not implemented or flashed.
- Even perfect hardware layer mapping might not eliminate the **RenderThread queue delay and app drawing** already independently measured in G1549/G1561/G1577. The physical G1568 shade GFXInfo still shows 82–85% janky frames. A before/after physical benchmark and on-LCD correctness are required to attribute HWC changes to app smoothness.

## Immutable evidence and offline source regression

Actual fixture file SHA256: HOME `63cc2930a6377aa6231df6c9cb34ba50b79cea74dabf6db0617501713fca08dd`; OPEN `b97bc8bfefadf12471ecbca42b05468ded27faede5ec33b2f2676744ebc9ee63`; CLOSE `928cee9444ac2e54603a87a521de9916dad213958b1764127c32c88aa1f3a204`; SUMMARY `827b29f260e515c826382d58b393230ba11cc9213036b7f5238ede3082934a38`.

Reproduce with `python3 device/samsung/lt013g/tools/test_g1588_fimd_hwc_physical_offload.py` and `python3 device/samsung/lt013g/tools/g1588_analyze_fimd_hwc_physical_layers.py --android-root /path/to/lineage-18.1-sm-t311 --kernel-root /path/to/kernel-active-g958`. The source checker fails on missing physical kernel/HWC safety guards, incorrect layer enums, corrupt state count, nonzero HWC composition errors and any modified raw sample SHA. It deliberately does not claim FPS gain nor change a display property. Source Git commits/QA report archived in ongoing Samsung Linux migration audit.

**Next**: Build a strictly opt-in *static planner simulation* and add multi-plane capability gating with tests for fixed z/alpha/pixel formats, then only compile and physically test when safe. Do not flash an unverified plane reorder or disable known-good graphics/GNSS/modem/kernel.