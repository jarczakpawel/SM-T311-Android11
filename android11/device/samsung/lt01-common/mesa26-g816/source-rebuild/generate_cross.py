#!/usr/bin/env python3
"""Render the verified Android11/ARMv7 Lima Meson cross-file template.

One-device recipe; source-only. Does not build a binary or touch the tablet.
"""
import argparse
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--android-root',required=True,type=Path)
p.add_argument('--link-lib-dir',required=True,type=Path,help='host directory of Android ARM32 linker libraries; must be prepared from the matching Android11 build')
p.add_argument('--out',required=True,type=Path)
a=p.parse_args()
android=a.android_root.resolve(strict=True)
link=a.link_lib_dir.resolve(strict=True)
if not (android/'build/soong/soong_ui.bash').exists():
    p.error('not an Android source root')
if not (android/'prebuilts/clang/host/linux-x86/clang-r383902/bin/clang').exists():
    p.error('expected Android11 pinned NDK/Clang r383902 toolchain is absent')
if not (android/'out/soong/ndk/sysroot').exists():
    p.error('Android11 generated NDK sysroot missing; build bootstrap first')
if not (android/'prebuilts/ndk/r21/platforms/android-29/arch-arm/usr/lib').exists():
    p.error('Android11 ARM NDK r21 platforms missing')
if not link.is_dir():p.error('link lib dir missing')
for lib in ('libc++.so','libc.so','libdl.so','libm.so','libz.so'):
    if not (link/lib).is_file():
        p.error(f'required ARM32 linker input missing or broken: {link/lib}')
template=(Path(__file__).parent/'armv7-android11.meson-cross.in').read_text()
assert template.count('@@ANDROID_ROOT@@')>=4 and template.count('@@ANDROID_LINK_LIBDIR@@')==2
out=template.replace('@@ANDROID_ROOT@@',str(android)).replace('@@ANDROID_LINK_LIBDIR@@',str(link))
a.out.parent.mkdir(parents=True,exist_ok=True)
a.out.write_text(out)
print('G870_MESA26_2_5_ANDROID_ARMV7_CROSS_FILE_SOURCE_READY',a.out)
