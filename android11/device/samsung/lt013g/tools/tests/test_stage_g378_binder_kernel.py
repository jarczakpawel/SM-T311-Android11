#!/usr/bin/env python3
"""G378 offline GPL source kernel staging positive and rejection tests."""
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

tools=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(tools))
mod=tools/"stage_g378_binder_kernel.py"
spec=importlib.util.spec_from_file_location("stage_g378",mod)
s=importlib.util.module_from_spec(spec);spec.loader.exec_module(s)

class StageGPLBinderKernel(unittest.TestCase):
    def setUp(self):
        self.t=tempfile.TemporaryDirectory();root=Path(self.t.name)
        self.private=root/"prebuilt";self.private.mkdir()
        self.z=root/"zImage";self.d=root/"tab3.dtb";self.v=root/"vmlinux"
        self.c=root/"config";self.old=root/"old.config";self.out=self.private/"kernel-g378.bin"
        self.z.write_bytes(b"ARMz"+bytes(4000000))
        self.d.write_bytes(b"\xd0\x0d\xfe\xed"+bytes(128))
        self.v.write_bytes(b"vmlinux\x00binder,hwbinder,vndbinder,lt013g-gps\x00")
        self.c.write_text('CONFIG_ANDROID_BINDER_DEVICES="binder,hwbinder,vndbinder,lt013g-gps"\nCONFIG_CPU_FREQ=y\n')
        self.old.write_text('CONFIG_ANDROID_BINDER_DEVICES="binder,hwbinder,vndbinder"\nCONFIG_CPU_FREQ=y\n')
    def tearDown(self):self.t.cleanup()
    def stage(self):
        return s.stage(self.z,self.d,self.v,self.c,self.old,self.out,self.private)
    def test_valid_kernel_stage_idempotent(self):
        self.assertEqual(self.stage()["status"],"PASS_STAGE_GPL_SOURCE_KERNEL")
        self.assertEqual(self.stage()["status"],"PASS_ALREADY_IDENTICAL")
        self.assertFalse(self.stage()["flash_ready"])
    def test_reject_extra_config_diff(self):
        self.c.write_text(self.c.read_text().replace("CONFIG_CPU_FREQ=y","CONFIG_CPU_FREQ=n"))
        with self.assertRaises(ValueError):self.stage()
    def test_reject_missing_compiled_param(self):
        self.v.write_bytes(b"vmlinux without binder")
        with self.assertRaises(ValueError):self.stage()
    def test_reject_outside_private(self):
        self.out=Path(self.t.name)/"PUBLIC_KERNEL.bin"
        with self.assertRaises(ValueError):self.stage()
    def test_do_not_replace_old_staged_binary(self):
        self.out.write_bytes(b"OLD_PROVEN_KERNEL")
        with self.assertRaises(ValueError):self.stage()
        self.assertEqual(self.out.read_bytes(),b"OLD_PROVEN_KERNEL")
    def test_reject_bad_dtb(self):
        self.d.write_bytes(b"BAD_DTB")
        with self.assertRaises(ValueError):self.stage()

if __name__=="__main__":unittest.main(verbosity=2)
