# SM-T311 Mesa 26.2.5 - source-only build inputs (G870)

These are the G815/G814 tested-source **scripts, six patches and cross-file template**, now kept within the Android11 SM-T311 device repository. No OEM firmware, compiled binaries, proprietary tarballs, passwords or device images are included. Targets **only SM-T311 (`lt013g`), Mali-400 ARMv7, Android 11, Mesa 26.2.5 with libdrm 2.4.133**.

Inputs: official Mesa `mesa-26.2.5.tar.xz` SHA256 `794b3d1729075dae7ebd47c7283b7ca0cdb5dec668f7f11cc0a5d82f6138a0a5`; official libdrm `libdrm-2.4.133.tar.xz` SHA256 `fc68f9d0ba2ea63c9432a299e14fea09fad7a8a66e8039fcd7802ca59f77b4f5`. Have Android11 ARMv7 toolchain, generated NDK headers, suitable ARM32 linker-library directory and Meson/Ninja/Python 3 with `mako==1.3.10` and PyYAML.

## G1523 release-grade cross-check (verified in real Debian workspace)

The source-only auditor `verify_bitexact_six.py` directly checks **all six actual Meson-compiled ELF32 ARM outputs** against both the tracked `mesa2625-arm32.sha256` manifest and the six ignored Android staged `.so` inputs. It refuses a noncanonical relative Meson build directory, missing upstream Mesa 26.2.5 source, missing Android EGL queueBuffer error handling, missing/mismatched binaries, and non-ARM outputs. It makes no tablet writes, no firmware/ROM changes, and no binary copies. On this Debian host:

```sh
python3 device/samsung/lt01-common/mesa26-g816/source-rebuild/verify_bitexact_six.py \
  --project /home/debian/Projekty/samsung \
  --android "$PWD" \
  --output /home/debian/Projekty/samsung/out/SM-T311-G872-MESA-26.2.5-SHALLOW
```

Physical production SHA match: **6/6 PASS** with the canonical G872 Meson build. Negative test against legacy `out/SM-T311-G815-MESA-26.2.5-LIMA-ARMV7` **properly refuses** wrong `libEGL.so` SHA, rather than accidentally implying that any successful build produces the deployed Mesa. The old G815 build-script output and its three differing filenames are **not acceptable release inputs**. Build with the canonical G872 `build_armv7.sh` and only stage after both this auditor and `stage.sh --verify-only` pass. This auditor does not claim a fresh out-of-tree toolchain rebuild; it verifies the existing independent G872 compiled output and is safe to rerun in future.

The **relative layout is part of the binary build contract**. Mesa embeds `__FILE__` and compiler-generated paths in ELF: the source directory must be `PROJECT/worktrees/mesa-26.2.5` and the Meson build directory must be `PROJECT/out/<a-single-build-directory>` (not `PROJECT/out/<name>/meson-armv7`). The build helper refuses any layout whose relative source path is not exactly `../../worktrees/mesa-26.2.5`. This was verified by independent 1020/1020 ARMv7 builds: a nested build returned 3/6 changed SHA, whereas a shallow build returned **all six bit-identical SHA** to the libraries running on the SM-T311.

On a **new machine**, provide the two official tarballs and a matching Android 11 ARM32 link-library directory (five `.so` linker dependencies: `libc`, `libdl`, `libm`, `libc++`, `libz`; build these from the matching Android source). The Meson/Python Mako 1.3.10/PyYAML, Android Clang r383902 and NDK headers must be installed. Run from Android repository root:

```bash
export SMT311_MESA_PROJECT_ROOT=/path/to/your/smt311-project
export SMT311_MESA_TARBALL=/path/to/mesa-26.2.5.tar.xz
export SMT311_LIBDRM_TARBALL=/path/to/libdrm-2.4.133.tar.xz
export SMT311_MESA_BUILD_DIR="$SMT311_MESA_PROJECT_ROOT/out/SM-T311-MESA2625-LOG"
# Canonical source directory must not already exist when first reconstructing:
device/samsung/lt01-common/mesa26-g816/source-rebuild/reconstruct_sources.sh
python3 device/samsung/lt01-common/mesa26-g816/source-rebuild/generate_cross.py \
  --android-root "$PWD" \
  --link-lib-dir /path/to/matching/android11/arm32-linker-libs \
  --out "$SMT311_MESA_BUILD_DIR/armv7-android11.cross"
export SMT311_MESA_CROSS_FILE="$SMT311_MESA_BUILD_DIR/armv7-android11.cross"
# Default Meson output: $SMT311_MESA_PROJECT_ROOT/out/SM-T311-MESA2625-ARMV7
# Default source path: $SMT311_MESA_PROJECT_ROOT/worktrees/mesa-26.2.5
device/samsung/lt01-common/mesa26-g816/source-rebuild/build_armv7.sh
# The build itself now verifies all six exact reference SHA, or returns exit 15.
export SMT311_MESA2625_INPUT_LIBDIR="$SMT311_MESA_BUILD_DIR/isolated-libs"
export SMT311_ANDROID_ROOT="$PWD"
device/samsung/lt01-common/mesa26-g816/stage.sh --verify-only
# Stage only the qualified exact SHA outputs into Git-ignored Android inputs:
device/samsung/lt01-common/mesa26-g816/stage.sh --stage
```

G871 built all six libraries 1020/1020 PASS but found different ELF strings and SHA for EGL/Gallium/GBM because the source path was `../mesa-26.2.5-reproducible` or `../../../worktrees/mesa-26.2.5`. G872 independently built all six 1020/1020 PASS using the pinned `../../worktrees/mesa-26.2.5` source path and **six of six SHA equal to the physical tablet**, with no binary edits. The guard makes incorrect path choices fail before a build, and the SHA check makes mismatching toolchain outputs fail even if the build completes.

Avoid `meson setup` in a directory one level deeper: that silently changes the ELF. Do not edit or normalize the ELF after compilation. Never copy an unqualified `.so` into the Android build, never commit it to Git, and never install it on the device before compatibility checks.

No TWRP/ADB operation occurs in these scripts. They do not approve a new Android ROM/ART/BOOT installation or override G868 strict release hold.
