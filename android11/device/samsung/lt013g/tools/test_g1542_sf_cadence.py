#!/usr/bin/env python3
"""No-ADB synthetic regression for Android11 SF --latency interval analysis."""
import importlib.util
from pathlib import Path
p=Path(__file__).with_name('g1542_analyze_sf_hwui_present_cadence.py')
spec=importlib.util.spec_from_file_location('g1542',p)
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
period=17107264
pres=[101000000000]
for multiplier in [1,2,1,1,3,1,1,1,1,1,1,1,1,1,1]:
    pres.append(pres[-1]+period*multiplier)
# a, b, c: timestamps must be parseable, no assumption about durations.
data=str(period)+'\n'+'\n'.join(str(b-1000)+'\t'+str(b)+'\t'+str(b+2000) for b in pres)
s=module.surface_stats(data)
assert s['frame_records_complete']==16
assert s['active_presentation_intervals']==15
assert s['deliberate_or_idle_gaps_over_250ms']==0
assert s['active_intervals_exceeding_1_5_vsync']==2
assert s['estimated_vsync_slots_between_presentations_not_displayed']==3
assert s['refresh_hz']==round(1e9/period,5)
# Idle pause must never be counted as 60 missed vsyncs.
more=data+'\n'+str(pres[-1]+1000000000-1000)+'\t'+str(pres[-1]+1000000000)+'\t'+str(pres[-1]+1000000000+2000)
s=module.surface_stats(more)
assert s['deliberate_or_idle_gaps_over_250ms']==1
assert s['estimated_vsync_slots_between_presentations_not_displayed']==3
# A ring may contain a zero row or pending fence INT64_MAX, which are excluded.
more=data+'\n0 0 0\n0 '+str((1<<63)-1)+' 0\n'
s=module.surface_stats(more)
assert s['frame_records_complete']==16
for content in ('nope', str(0)+'\n'+data, '17107264\n0 1 2\n'):
    try:module.surface_stats(content)
    except ValueError:pass
    else:raise AssertionError('invalid input not rejected')
paired=module.paired_renderthread_window([
    {'Flags':0,'SyncQueued':10_000_000,'SwapBuffers':12_000_000},
    {'Flags':0,'SyncQueued':20_000_000,'SwapBuffers':54_000_000},
    {'Flags':0,'SyncQueued':100_000_000,'SwapBuffers':122_000_000},
    {'Flags':1,'SyncQueued':1,'SwapBuffers':999_999_999},
], period)
assert paired['paired_valid_frames']==3
assert paired['over_1_display_refresh_budget']==2
assert paired['p50_ms']==22.0
assert paired['p90_ms']==34.0
# Public Git fixture: independently reproduces the physically measured
# SF cadence and identical-run Android11 HWUI frame-stage numbers.
data_root=Path(__file__).with_name('testdata')
report=module.report(
    data_root/'g1541-physical-notificationshade-sf-latency.txt',
    data_root/'g1541-physical-notificationshade-hwui-framestats.txt',
    data_root/'g1541-physical-notificationshade-summary.txt')
assert report['surface']['frame_records_complete']==39
assert report['surface']['refresh_ns']==17107264
assert report['surface']['active_presentation_intervals']==37
assert report['surface']['active_intervals_exceeding_1_5_vsync']==15
assert report['surface']['active_interval_p50_ms']==17.112
assert report['surface']['active_interval_p90_ms']==34.214
assert report['hwui_notification_shade']['flags_zero']==114
assert report['hwui_same_frame_syncqueued_to_swap']['paired_valid_frames']==114
assert report['hwui_same_frame_syncqueued_to_swap']['over_1_display_refresh_budget']==97
assert report['hwui_same_frame_syncqueued_to_swap']['p50_ms']==30.647
assert report['hwui_same_frame_syncqueued_to_swap']['p90_ms']==45.276
assert report['raw_sha256']['sf_latency']=='ce561deeaae98d86990c384660c69720b0dec9ffba64077e8175b425f1af4380'
assert report['raw_sha256']['hwui_framestats']=='43bdacf0f7d6f06f779f96b3b702727729a5efa4d6c96300d9a2c2faf49f707e'
assert report['raw_sha256']['physical_summary']=='a2f2da42e74ec51e85ef9255c95eca0439f14a45df4cd7dfd7feb17c5c4cd44e'
print('G1547_PHYSICAL_58HZ_SF_39_PRESENT_AND_114_PAIRED_HWUI_FRAMES_FIXTURE_PASS')
print('G1542_SURFACEFLINGER_PRESENT_CADENCE_SYNTHETIC_INTERVAL_IDLE_AND_PENDING_REGRESSION_PASS')
