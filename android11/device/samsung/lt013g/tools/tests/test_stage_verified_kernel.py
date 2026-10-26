#!/usr/bin/env python3
"""Host tests: reject mismatched BOOT / unsafe output / unintended overwrite."""
import importlib.util
import struct
import tempfile
from pathlib import Path
import unittest

p=Path(__file__).resolve().parents[1]/"stage_verified_kernel.py"
spec=importlib.util.spec_from_file_location("verified_stage",p)
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class VerifiedStageTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory()
        self.root=Path(self.tmp.name)
        self.base=self.root/"prebuilt"
        self.base.mkdir()
        self.z=self.root/"zImage"
        self.d=self.root/"dtb"
        self.boot=self.root/"boot.img"
        self.out=self.base/"kernel.bin"
        self.z.write_bytes(b"ZIMAGE_VALID")
        self.d.write_bytes(b"DTB_VALID")
        h=bytearray(2048);h[:8]=b"ANDROID!"
        struct.pack_into("<I",h,8,len(self.z.read_bytes()+self.d.read_bytes()))
        struct.pack_into("<II",h,36,2048,0)
        self.boot.write_bytes(h+self.z.read_bytes()+self.d.read_bytes())
    def tearDown(self):self.tmp.cleanup()
    def test_stage_new_and_identical(self):
        x=module.stage(self.z,self.d,self.boot,self.out,self.base)
        self.assertEqual(x["status"],"PASS_STAGED_SOURCE_KERNEL")
        x=module.stage(self.z,self.d,self.boot,self.out,self.base)
        self.assertEqual(x["status"],"PASS_ALREADY_IDENTICAL")
    def test_no_overwrite(self):
        self.out.write_bytes(b"wrong")
        with self.assertRaisesRegex(ValueError,"Existing staged"):
            module.stage(self.z,self.d,self.boot,self.out,self.base)
        self.assertEqual(self.out.read_bytes(),b"wrong")
    def test_mismatched_binary_not_staged(self):
        self.z.write_bytes(b"modified")
        with self.assertRaises(ValueError):
            module.stage(self.z,self.d,self.boot,self.out,self.base)
        self.assertFalse(self.out.exists())
    def test_wrong_boot(self):
        self.boot.write_bytes(b"garbage")
        with self.assertRaises(ValueError):
            module.stage(self.z,self.d,self.boot,self.out,self.base)
    def test_path_outside_private_prebuilt_blocked(self):
        with self.assertRaisesRegex(ValueError,"Only local"):
            module.stage(self.z,self.d,self.boot,self.root/"PUBLIC_K.bin",self.base)
    def test_header_version_rejected(self):
        b=bytearray(self.boot.read_bytes());struct.pack_into("<I",b,40,2)
        self.boot.write_bytes(b)
        with self.assertRaises(ValueError):
            module.stage(self.z,self.d,self.boot,self.out,self.base)

if __name__=="__main__":unittest.main(verbosity=2)
