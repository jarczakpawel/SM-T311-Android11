#!/usr/bin/env python3
"""SM-T311 G1523: verify reproducible Lima Mesa 26.2.5 without device writes.

Checks exact *compiled* six ELF SHA against both the immutable device manifest
and the staged inputs. Crucially checks the Meson-relative source/build path,
which changes EGL/Gallium/GBM ELF even when every C source line is identical.
"""
import argparse
import hashlib
import os
import subprocess
from pathlib import Path


def sha(path):
    with path.open('rb') as stream:
        digest = hashlib.sha256()
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
        return digest.hexdigest()


def check(project, android, out):
    project = project.resolve(strict=True)
    android = android.resolve(strict=True)
    out = out.resolve(strict=True)
    source = project / 'worktrees/mesa-26.2.5'
    if not source.is_dir() or (source / 'VERSION').read_text().strip() != '26.2.5':
        raise ValueError('G1523_REFUSE_MESA_SOURCE_VERSION_OR_PATH')
    if out.parent != project / 'out' or os.path.relpath(source, out) != '../../worktrees/mesa-26.2.5':
        raise ValueError('G1523_REFUSE_MESA_WRONG_MESON_RELATIVE_BUILD_PATH')
    root = android / 'device/samsung/lt01-common/mesa26-g816'
    manifest = root / 'mesa2625-arm32.sha256'
    stage = android / 'device/samsung/lt01-common/prebuilt/pjarczak/mesa26-2625'
    if not manifest.is_file() or not stage.is_dir():
        raise ValueError('G1523_REFUSE_STAGING_OR_MANIFEST_MISSING')
    from_build = {
        'libEGL.so': 'src/egl/libEGL.so',
        'libGLESv1_CM.so': 'src/mesa/glapi/es1api/libGLESv1_CM.so',
        'libGLESv2.so': 'src/mesa/glapi/es2api/libGLESv2.so',
        'libgallium_dri.so': 'src/gallium/targets/dri/libgallium_dri.so',
        'libgbm_mesa.so': 'src/gbm/libgbm_mesa.so',
        'libdrm_mesa26.so': 'subprojects/libdrm-2.4.133/libdrm_mesa26.so',
    }
    rows = [line.split() for line in manifest.read_text().splitlines() if line.strip()]
    if len(rows) != 6 or len({row[0] for row in rows}) != 6 or set(row[0] for row in rows) != set(from_build) or any(len(row) != 3 for row in rows):
        raise ValueError('G1523_REFUSE_MANIFEST_NOT_EXACT_SIX')
    if not (source / 'src/egl/drivers/dri2/platform_android.c').is_file():
        raise ValueError('G1523_REFUSE_EGL_QUEUE_SOURCE_NOT_PRESENT')
    source_egl = (source / 'src/egl/drivers/dri2/platform_android.c').read_text()
    for text in ('ANativeWindow_cancelBuffer after queue failure', 'dri2_surf->base.Lost = EGL_TRUE'):
        if text not in source_egl:
            raise ValueError('G1523_REFUSE_ACCEPTED_QUEUE_BUFFER_HARDENING_MISSING')
    for name, dest, expected in rows:
        built = out / from_build[name]
        staged = stage / dest
        if not built.is_file() or not staged.is_file() or built.is_symlink() or staged.is_symlink():
            raise ValueError('G1523_REFUSE_MESA_BINARY_MISSING_OR_SYMLINK_' + name)
        if sha(built) != expected:
            raise ValueError('G1523_REFUSE_WRONG_SOURCE_BUILT_MESA_SHA_' + name)
        if sha(staged) != expected:
            raise ValueError('G1523_REFUSE_WRONG_STAGED_MESA_SHA_' + dest)
        elf = subprocess.check_output(['readelf', '-h', str(built)], text=True)
        if not all(part in elf for part in ('ELF32', 'ARM', 'little endian')):
            raise ValueError('G1523_REFUSE_NOT_ARMV7_ELF_' + name)
        print('G1523_EXACT_PHYSICAL_MESA_SOURCE_REBUILD_SHA_PASS', name, expected)
    print('G1523_ALL_SIX_BITEXACT_SOURCE_BUILT_VS_STAGED_MESA_PASS')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--project', required=True, type=Path)
    parser.add_argument('--android', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    try:
        check(args.project, args.android, args.output)
    except (ValueError, FileNotFoundError, subprocess.CalledProcessError) as error:
        parser.exit(23, str(error) + '\n')
