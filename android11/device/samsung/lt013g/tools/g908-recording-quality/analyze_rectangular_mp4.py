#!/usr/bin/env python3
"""G908: read-only per-decoded-frame MP4 orientation/letterbox/PTS QA.

Accepts a PRIVATE local MP4, emits aggregate metadata only. Never commits
video, captured frames, users' screen contents or full logcat. Requires the
standard Python library and host ffmpeg+ffprobe. This detects letterboxing,
not arbitrary in-frame 90-degree rotations (those need visual confirmation).
"""
import argparse
import json
import subprocess

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('video')
ap.add_argument('--fail-on-letterbox', action='store_true')
ap.add_argument('--fail-on-h264', action='store_true',
                help='fail on any independently decoded H264 entropy corruption')
a = ap.parse_args()
video = a.video
probe = json.loads(subprocess.check_output([
    'ffprobe', '-v', 'error', '-select_streams', 'v:0', '-show_frames',
    '-show_entries', 'frame=best_effort_timestamp_time,key_frame',
    '-of', 'json', video], text=True, timeout=60))
metadata = probe['frames']
width, height = 160, 256
f = subprocess.run([
    'ffmpeg', '-nostdin', '-v', 'error', '-threads', '2', '-i', video,
    '-vf', f'scale={width}:{height}', '-fps_mode', 'passthrough',
    '-pix_fmt', 'gray', '-f', 'rawvideo', '-'],
    stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=70)
assert f.returncode == 0, 'could not decode MP4'
assert len(f.stdout) % (width * height) == 0, 'bad raw frame count'
count = len(f.stdout) // (width * height)
assert count == len(metadata), f'frame count mismatch {count} != {len(metadata)}'
frame_bytes = memoryview(f.stdout)
letterbox = []
transitions = []
previous = False
for idx in range(count):
    frame = frame_bytes[idx * width * height:(idx + 1) * width * height]
    rows = [sum(v > 18 for v in frame[y * width:(y + 1) * width]) for y in range(height)]
    active = [y for y, n in enumerate(rows) if n >= 7]
    top = active[0] if active else height
    bottom = active[-1] if active else -1
    bars = (top > 20 and height - 1 - bottom > 20)
    ts = float(metadata[idx]['best_effort_timestamp_time'])
    if bars:
        letterbox.append((idx, ts, top * 4, (height - 1 - bottom) * 4))
    if idx == 0 or bars != previous:
        transitions.append((idx, round(ts, 4), 'LETTERBOX' if bars else 'FULL'))
    previous = bars
pts = [float(m['best_effort_timestamp_time']) for m in metadata]
assert all(b > aa for aa, b in zip(pts, pts[1:])), 'PTS non-monotonic'
long_gaps = [(i + 1, round((b - aa)*1000, 1)) for i, (aa, b) in enumerate(zip(pts, pts[1:])) if (b - aa) > .2]
keys = [i for i, m in enumerate(metadata) if m['key_frame'] == 1]
print(f'FRAMES={count} DURATION_S={pts[-1]:.4f} AVG_FPS={count/(pts[-1]-pts[0]):.3f} KEYFRAMES={len(keys)}')
print(f'G908_BARS_FRAMES={len(letterbox)} PERCENT={len(letterbox)/count*100:.2f} TRANSITIONS={transitions}')
print(f'G908_INTERFRAME_GAPS_OVER_200MS={len(long_gaps)} MAX_GAP_MS={max((v for _, v in long_gaps), default=0)}')
# G920: FFmpeg may decode a corrupt H.264 picture with return code zero
# after error concealment. A *successful process exit* does not prove video
# integrity. Keep a strict, independent entropy error gate for MFC output.
error_signatures = ('error while decoding MB', 'corrupt decoded frame',
                    'out of range intra chroma pred mode', 'Invalid level prefix',
                    'cbp too large', 'dquant out of range',
                    'negative number of zero coeffs')
errors = [line for line in f.stderr.decode('utf-8', 'replace').splitlines()
          if any(pattern in line for pattern in error_signatures)]
print(f'G908_FFMPEG_MACROBLOCK_DECODE_ERROR_LINES={sum("error while decoding MB" in line for line in errors)}')
print(f'G920_H264_DECODER_ERROR_MESSAGES={len(errors)}')
if a.fail_on_letterbox:
    assert len(letterbox) == 0, 'G908_ROTATION_RESIZE_REGRESSION_LETTERBOX_PRESENT'
if a.fail_on_letterbox or a.fail_on_h264:
    assert not errors, 'G920_H264_BITSTREAM_ENTROPY_INTEGRITY_FAIL'
print('G908_VIDEO_FRAME_BY_FRAME_REPORT_COMPLETE')
