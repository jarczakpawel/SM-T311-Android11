#!/usr/bin/env python3
"""Check SM-T311 AVC rectangle and periodic IDR cadence from a real MP4.

Read-only. Requires local ffprobe, not an Android device or a private clip.
Caller provides a capture to analyze; no private recordings enter Git.
"""
import argparse
import json
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('video', type=Path)
parser.add_argument('--expect-width', type=int, required=True)
parser.add_argument('--expect-height', type=int, required=True)
parser.add_argument('--min-frames', type=int, default=61)
parser.add_argument('--max-gop-frames', type=int, default=30)
a = parser.parse_args()
if (a.expect_width,a.expect_height) not in ((640,1024),(1024,640)):
    parser.error('G886 applies exclusively to the two qualified rectangles')
if a.min_frames < a.max_gop_frames * 2 + 1:
    parser.error('need enough frames to prove multiple keyframe intervals')
cmd = ['ffprobe','-v','error','-select_streams','v:0','-show_streams','-show_packets',
       '-show_entries','stream=codec_name,width,height:packet=pts_time,dts_time,flags',
       '-of','json',str(a.video)]
report=json.loads(subprocess.check_output(cmd,text=True,timeout=40))
streams=report.get('streams',[])
assert len(streams)==1, f'bad video stream count: {len(streams)}'
v=streams[0]
assert v.get('codec_name')=='h264',f'expected hardware H264, got {v.get("codec_name")}'
assert (v.get('width'),v.get('height'))==(a.expect_width,a.expect_height),(v.get('width'),v.get('height'))
packets=report.get('packets',[])
assert len(packets)>=a.min_frames, f'insufficient encoded frames: {len(packets)} < {a.min_frames}'
keys=[i for i,p in enumerate(packets) if 'K' in p.get('flags','')]
assert len(keys)>=3, f'no periodic IDR: {keys}'
assert keys[0]==0, f'first frame not keyframe: {keys}'
gaps=[y-x for x,y in zip(keys,keys[1:])]
assert all(1<=g<=a.max_gop_frames for g in gaps),f'GOP too long: {gaps}'
for field in ('pts_time','dts_time'):
    times=[float(p[field]) for p in packets]
    assert all(b>a for a,b in zip(times,times[1:])),f'non-increasing {field}'
print(f'G886_RECTANGULAR_H264_PASS {v["width"]}x{v["height"]} frames={len(packets)}'
      f' keyframes={len(keys)} inter_idr_frames={gaps[:15]} strict_pts_dts=PASS')
