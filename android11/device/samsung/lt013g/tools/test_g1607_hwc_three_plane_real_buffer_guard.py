#!/usr/bin/env python3
"""G1607 fail-closed source contract: no synthetic true placeholders allowed.

G1592's older HWC planner accepted any three layers when enabled, including a
CLIENT framebuffer and two DEVICE layers. Each planner leg is now reevaluated
from actual DrmHwcLayer buffer provenance, gralloc usage, format and geometry.
"""
from pathlib import Path
HWC=Path(__file__).resolve().parents[4]/'external/drm_hwcomposer'
P=(HWC/'platform/platform.cpp').read_text()
D=(HWC/'drmhwctwo.cpp').read_text()
S=(HWC/'include/drmhwcomposer.h').read_text()
assert 'bool is_client_target = false;' in S
assert 'layer.is_client_target = (l.second == &client_layer_);' in D
assert 'layer.is_client_target || layer.protected_usage()' in P
assert 'static_cast<uint64_t>(layer.buffer->width) * bytes_per_pixel' in P
assert 'format == DRM_FORMAT_RGB565 || format == DRM_FORMAT_BGR565 ? 2 :' in P
assert 'format == DRM_FORMAT_ARGB8888 || format == DRM_FORMAT_ABGR8888 ? 4 : 0' in P
assert 'GRALLOC_USAGE_PROTECTED' in P
assert 'layers.size() == 3 && layers.count(0) && layers.count(1)' in P
assert 'layers.count(2)' in P
assert 'layer.buffer->width >= static_cast<uint32_t>(w)' in P
assert 'layer.buffer->height >= static_cast<uint32_t>(h)' in P
assert 'layer.source_crop.left == 0.0f' in P
assert 'layer.source_crop.top == 0.0f' in P
assert 'layer.source_crop.right == static_cast<float>(w)' in P
assert 'layer.source_crop.bottom == static_cast<float>(h)' in P
assert 'middle.blending == DrmHwcBlending::kNone' in P
assert 'middle.alpha == 0xffff' in P
assert 'opaque_format(middle.buffer->format)' in P
assert 'opaque_format(bottom.buffer->format)' in P
assert 'top_format(top.buffer->format)' in P
assert 'middle.display_frame.left > screen.left' in P
assert 'top.blending == DrmHwcBlending::kPreMult' in P
assert 'planes = {win1, win2, win3};' in P
assert 'strcmp(enabled, "1") == 0' in P
assert 'property_get("persist.vendor.t311.hwc_win1_3plane", enabled, "0")' in P
assert '      true, true, true, true,\n' not in P
assert 'bottom_fullscreen_opaque, middle_partial_opaque,\n      top_non_scaling_buffer, all_unprotected,' in P
for tampered in ('      true, true, true, true,', 'client_layer_.buffer() == nullptr'):
    assert tampered not in P
print('G1607_HWC_CLIENT_TARGET_PROVENANCE_ALL_3_BUFFER_FORMAT_Z_ALPHA_CROP_GUARDS_PASS')
