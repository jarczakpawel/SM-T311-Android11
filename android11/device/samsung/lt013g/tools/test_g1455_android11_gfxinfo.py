#!/usr/bin/env python3
import hashlib
import importlib.util
import unittest
from pathlib import Path

source = Path(__file__).with_name("g1455-analyze-android11-gfxinfo-framestats.py")
spec = importlib.util.spec_from_file_location("g1455", source)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
HEADER = ("Flags,IntendedVsync,Vsync,OldestInputEvent,NewestInputEvent,"
          "HandleInputStart,AnimationStart,PerformTraversalsStart,DrawStart,"
          "SyncQueued,SyncStart,IssueDrawCommandsStart,SwapBuffers,FrameCompleted,"
          "DequeueBufferDuration,QueueBufferDuration,GpuCompleted,")
FRAME = [0, 1000000000, 1000000000, 0, 0, 1001000000, 1002000000,
         1002100000, 1003000000, 1004000000, 1007000000,
         1008000000, 1016000000, 1020000000, 2000000, 1000000, -1]

def frame(values):
    return ",".join(map(str, values)) + ","

def text(window="NotificationShade", frames=None):
    frames = frames if frames is not None else [FRAME]
    return "\n".join(["Window: " + window, "---PROFILEDATA---", HEADER,
                      *(frame(row) for row in frames), "---PROFILEDATA---"])

class FrameStagesTest(unittest.TestCase):
    def test_synthetic_pipeline(self):
        stats = module.analyze(text())["NotificationShade"]
        self.assertEqual(stats["retained_frames"], 1)
        self.assertEqual(stats["flags_zero"], 1)
        self.assertEqual(stats["stages_ms"]["renderthread_queue_wait"]["p50"], 3)
        self.assertEqual(stats["stages_ms"]["commands_to_swap"]["p50"], 8)
        self.assertEqual(stats["stages_ms"]["DequeueBufferDuration"]["p50"], 2)

    def test_physical_device_capture_fixture(self):
        path = source.parent / "testdata/g1455-physical-systemui-framestats.txt"
        raw = path.read_bytes()
        self.assertEqual(hashlib.sha256(raw).hexdigest(),
                         "43af3e708caaddb7706485edb9cf3af5a55cc64f1a42ef1ed06cfb8232e94b68")
        stats = module.analyze(raw.decode())["NotificationShade"]
        self.assertEqual((stats["retained_frames"], stats["flags_zero"]), (120, 114))
        self.assertEqual(stats["stages_ms"]["renderthread_queue_wait"]["p50"], 8.818)
        self.assertEqual(stats["stages_ms"]["commands_to_swap"]["p90"], 26.185)
        self.assertEqual(stats["stages_ms"]["DequeueBufferDuration"]["p50"], 6.461)

    def test_flags_and_absent_window(self):
        flagged = FRAME.copy()
        flagged[0] = 1
        stats = module.analyze(text(frames=[FRAME, flagged]))["NotificationShade"]
        self.assertEqual(stats["flags_zero"], 1)
        self.assertEqual(stats["layout_or_other_flagged"], 1)
        with self.assertRaises(ValueError):
            module.analyze(text(window="StatusBar"))

    def test_zero_and_invalid_time(self):
        invalid = FRAME.copy()
        invalid[10] = invalid[9] - 1
        stats = module.analyze(text(frames=[invalid]))["NotificationShade"]
        self.assertEqual(stats["stages_ms"]["renderthread_queue_wait"]["count"], 0)
        self.assertIsNone(stats["stages_ms"]["renderthread_queue_wait"]["p50"])

if __name__ == "__main__":
    unittest.main()
