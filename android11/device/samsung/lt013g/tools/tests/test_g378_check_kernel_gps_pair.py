#!/usr/bin/env python3
"""Synthetic acceptance+negative tests of G378 paired binder kernel/GPS image."""
from copy import deepcopy
import importlib.util
from pathlib import Path
import struct
import tempfile
from zipfile import ZipFile
import unittest

path=Path(__file__).resolve().parents[1]/"g378_check_kernel_gps_pair.py"
spec=importlib.util.spec_from_file_location("g378pair",path)
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

class KernelGpsPairTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();base=Path(self.tmp.name)
        self.zip=base/"candidate.zip";self.z=base/"zImage";self.d=base/"device.dtb"
        self.cfg=base/"current";self.old=base/"old";self.l=base/"launcher"
        self.z.write_bytes(b"ZIMAGE-ARM");self.d.write_bytes(b"EXYNOS4212-DTB")
        self.l.write_bytes(b"ELF-GPS-BINDER-ISOLATED")
        self.old.write_text('CONFIG_ANDROID_BINDER_DEVICES="binder,hwbinder,vndbinder"\nCONFIG_CPU_FREQ=y\n')
        self.cfg.write_text(m.EXPECTED+'\nCONFIG_CPU_FREQ=y\n')
        self.package()
    def tearDown(self):self.tmp.cleanup()
    def package(self,kernel=None,launch=None):
        key=kernel if kernel is not None else self.z.read_bytes()+self.d.read_bytes()
        header=bytearray(2048);header[:8]=b"ANDROID!"
        struct.pack_into("<I",header,8,len(key))
        struct.pack_into("<II",header,36,2048,0)
        with ZipFile(self.zip,"w") as zp:
            zp.writestr(m.BOOT,bytes(header)+key)
            zp.writestr(m.LAUNCHER,launch if launch is not None else self.l.read_bytes())
    def validate(self):return m.validate(self.zip,self.z,self.d,self.cfg,self.l,self.old)
    def test_exact_pair_allows_only_offline_acceptance(self):
        r=self.validate()
        self.assertEqual(r["status"],"PASS_OFFLINE_PAIR_NOT_FLASH_READY")
        self.assertFalse(r["flash_ready"])
        self.assertEqual(r["candidate_config"]["config_only_diff_keys"],["CONFIG_ANDROID_BINDER_DEVICES"])
    def test_mismatch_kernel_blocks(self):
        self.package(kernel=b"WRONG_KERNEL")
        self.assertEqual(self.validate()["status"],"BLOCK_KERNEL_GNSS_MISMATCH")
    def test_launcher_old_blocks(self):
        self.package(launch=b"OLD-G373-LAUNCHER")
        self.assertEqual(self.validate()["status"],"BLOCK_KERNEL_GNSS_MISMATCH")
    def test_missing_fourth_device_blocks(self):
        self.cfg.write_text(self.old.read_text())
        self.assertEqual(self.validate()["status"],"BLOCK_KERNEL_GNSS_MISMATCH")
    def test_extra_kernel_config_change_blocks(self):
        self.cfg.write_text(m.EXPECTED+"\nCONFIG_CPU_FREQ=n\n")
        self.assertEqual(self.validate()["status"],"BLOCK_KERNEL_GNSS_MISMATCH")
    def test_bad_header_rejected(self):
        with ZipFile(self.zip,"w") as f:
            f.writestr(m.BOOT,b"garbage")
            f.writestr(m.LAUNCHER,self.l.read_bytes())
        with self.assertRaises(ValueError):self.validate()
    def test_old_format_exact_rejects_disabled_binding(self):
        self.assertFalse(m.check_config(self.old.read_text(),self.old.read_text())["config_valid"])

if __name__=="__main__":unittest.main(verbosity=2)
