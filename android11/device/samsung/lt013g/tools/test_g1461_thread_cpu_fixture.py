#!/usr/bin/env python3
import hashlib
import unittest
from pathlib import Path

FIXTURE = Path(__file__).parent / "testdata/g1460-silent-systemui-surfaceflinger-cpu.txt"
SHA = "9cc07d11ed5baf5ed497658a47434f97c008c22342c85d687f20680616f714ec"

class PhysicalThreadCounterTest(unittest.TestCase):
    def test_exact_physical_receipt(self):
        data = FIXTURE.read_bytes()
        self.assertEqual(hashlib.sha256(data).hexdigest(), SHA)
        records = [row.split() for row in data.decode().splitlines()]
        self.assertEqual(len(records), 17)
        self.assertEqual(records[0], ["SystemUI", "898", "RenderThread", "2400"])
        by_tid = {(row[0], row[1]): (row[2], int(row[3])) for row in records}
        self.assertEqual(by_tid["SystemUI", "898"], ("RenderThread", 2400))
        self.assertEqual(by_tid["SystemUI", "657"], ("ndroid.systemui", 1200))
        self.assertEqual(by_tid["SurfaceFlinger", "223"], ("surfaceflinger", 730))

    def test_tick_units_and_total_cpu_budget(self):
        records = [line.split() for line in FIXTURE.read_text().splitlines()]
        for row in records:
            self.assertEqual(len(row), 4)
            self.assertIn(row[0], ("SystemUI", "SurfaceFlinger"))
            self.assertGreater(int(row[3]), 0)
            self.assertEqual(int(row[3]) % 10, 0)
        self.assertLessEqual(sum(int(row[3]) for row in records), 21480 * 2)

if __name__ == "__main__":
    unittest.main()
