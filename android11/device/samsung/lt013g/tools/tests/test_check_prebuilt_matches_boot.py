#!/usr/bin/env python3
"""Offline tests for product kernel vs physically proven BOOT v0 kernel."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

target=Path(__file__).resolve().parents[1]/"check_prebuilt_matches_boot.py"
spec=importlib.util.spec_from_file_location("prebuilt_kernel_gate",target)
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class PrebuiltKernelGateTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory()
        base=Path(self.tmp.name)
        self.boot=base/"boot.img"
        self.kernel=base/"kernel"
        header=bytearray(2048)
        header[:8]=b"ANDROID!"
        self.valid=b"KERNEL_BINARY_DTB"
        struct.pack_into("<10I",header,8,len(self.valid),0x10008000,3,0,0,0,0,2048,0,0)
        self.boot.write_bytes(header+self.valid+b"\x00"*2000)
        self.kernel.write_bytes(self.valid)
    def tearDown(self):
        self.tmp.cleanup()
    def test_exact(self):
        d=module.inspect(self.kernel,self.boot)
        self.assertTrue(d["equal"])
        self.assertEqual(d["status"],"PASS_EXACT_KERNEL")
        self.assertFalse(d["flash_ready"])
    def test_stale(self):
        self.kernel.write_bytes(self.valid+b"x")
        self.assertEqual(module.inspect(self.kernel,self.boot)["status"],
                         "BLOCK_PRODUCT_KERNEL_MISMATCH")
    def test_wrong_boot(self):
        self.boot.write_bytes(b"broken")
        with self.assertRaises(ValueError):module.inspect(self.kernel,self.boot)
    def test_reject_kernel_size_out_of_bounds(self):
        b=bytearray(self.boot.read_bytes())
        struct.pack_into("<I",b,8,99999999)
        self.boot.write_bytes(b)
        with self.assertRaises(ValueError):module.inspect(self.kernel,self.boot)
    def test_reject_header_version(self):
        b=bytearray(self.boot.read_bytes())
        struct.pack_into("<I",b,40,1)
        self.boot.write_bytes(b)
        with self.assertRaises(ValueError):module.inspect(self.kernel,self.boot)

if __name__=="__main__":unittest.main(verbosity=2)
