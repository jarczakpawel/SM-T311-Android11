# G867 - SM-T311 production Mesa 26.2.5 release lock

The physical SM-T311 Android 11 release runs Mesa **26.2.5** with six ARM32 components. The prior G816 opt-in could silently build legacy 26.2.4 when `SMT311_MESA26_VERSION` was unset. **The release changes the real `device/samsung/lt013g/device.mk` default to Mesa 26.2.5** and makes `common.mk` reject missing/altered private libraries, old versions or disabled Mesa. No global environment variable is needed for the correct version.

Reconstruct six private, Git-ignored binaries from the SHA-pinned upstream source using the **source-only G870 `source-rebuild/` scripts now committed in this same Android device repository** (see `source-rebuild/README.md`); this still requires the official tarballs, working ARM32 toolchain, and SHA validation. The source build may produce different binary hashes and those outputs must not be staged as the qualified release unless all six pinned SHA pass; the relevant source checksums and individual ARM32 output SHA256 are in `mesa2625-arm32.sha256`. From the Android root run:

```bash
# First ensure original private G815 build outputs exist and match source hashes:
device/samsung/lt01-common/mesa26-g816/stage.sh --verify-only
# Copy validated 26.2.5 libraries into ignored ROM build inputs:
device/samsung/lt01-common/mesa26-g816/stage.sh --stage
# Independently verify all six staged ARM32 libraries:
device/samsung/lt01-common/mesa26-g816/stage.sh --verify-staged
# Test real GNU Make release defaults and negative/rollback behavior:
python3 device/samsung/lt01-common/mesa26-g816/test_product_selector.py --device-dir device/samsung/lt01-common
```

Without the six exact staged files, product Make fails closed. Four product-copy mappings install EGL, GLES1, GLES2 and Gallium/Lima. GBM and libdrm_mesa26 are installed as **two explicit vendor `BUILD_PREBUILT` shared-library modules**, declared in `external/mesa3d/src/gbm/Android.mk` and `device/samsung/lt01-common/Android.mk`. GBM lists `libdrm_mesa26 liblog` in `LOCAL_SHARED_LIBRARIES`; Android verifies actual ELF `DT_NEEDED` dependencies, rather than bypassing verification. Neither shared module has a conflicting `PRODUCT_COPY_FILES` rule. This does **not** replace the independent Android `/system/lib/libEGL.so` or other framework/native GLES fixes.

Obsolete Mesa **26.2.4** has been removed from the current device source tree and product-selection logic. `SMT311_USE_MESA26=false`, an empty version, and any version other than `26.2.5` are rejected. Historical old binaries remain accessible only through earlier Git commits, not the current working release.

All six generated binaries are ignored by Git and must not be uploaded to public GitHub. When making an Android image after changing source product files, regenerate Kati/Soong product build metadata and rebuild the copy-file targets. **A changed Makefile alone does not retroactively change old files in `out/target/product/...`**. The release checker must refuse images whose six Mesa hashes do not match the reference. This gate is independent of physical tablet installations and does not authorize an OTA or BOOT flash.
