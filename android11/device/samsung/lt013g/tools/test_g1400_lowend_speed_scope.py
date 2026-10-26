#!/usr/bin/env python3
from pathlib import Path
script=(Path(__file__).resolve().parent/"g1400-lowend-launcher-art-ui-speed.sh").read_text()
for exact in (
    'cmd package compile -m speed -f com.android.launcher3',
    'settings put global window_animation_scale 0.5',
    'settings put global transition_animation_scale 0.5',
    'settings put global animator_duration_scale 0.5',
    '[ "$(getprop debug.lt013g.gps.v357.lto)" = 0 ]',
    'G1400_LAUNCHER_AOT_AND_FAST_UI_SETTINGS_PASS'):
    assert exact in script, exact
for unsafe in ('pm clear', 'pm uninstall', 'pm disable-user', 'setprop', 'ro.config.low_ram=true',
               'sound_effects_enabled 0', 'lockscreen_sounds_enabled 0',
               'settings put system volume_', 'setenforce', 'reboot'):
    assert unsafe not in script, unsafe
print('G1400_TARGETED_ART_AND_ANIMATIONS_WITHOUT_HARDWARE_CHANGE_SOURCE_PASS')
