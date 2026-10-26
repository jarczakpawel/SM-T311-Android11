#!/usr/bin/env python3
"""Synthetic zip-level tests for G374 ART bootclasspath safety gate."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
import zipfile

TARGET=Path(__file__).resolve().parents[1]/"check_carrier_art_release.py"
spec=importlib.util.spec_from_file_location("carrier_art_gate",TARGET)
m=importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

class CarrierArtReleaseGateTests(unittest.TestCase):
    def setUp(self):
        self.d=tempfile.TemporaryDirectory()
        self.root=Path(self.d.name)
        self.baseline=self.root/"old.zip"
        self.candidate=self.root/"new.zip"
        self.orig={k:f"old/{i}".encode() for i,k in enumerate(m.PATHS)}
        self.write(self.baseline,self.orig)
    def tearDown(self):self.d.cleanup()
    def write(self,name,values):
        with zipfile.ZipFile(name,"w",zipfile.ZIP_DEFLATED) as z:
            for k,v in values.items():z.writestr(k,v)
    def test_stale_jar_must_block(self):
        x=dict(self.orig);x[m.JAR]=b"new/telephony.jar"
        self.write(self.candidate,x)
        d=m.inspect(self.baseline,self.candidate,True)
        self.assertEqual(d["status"],"BLOCK_STALE_BOOT_ART")
        self.assertFalse(d["flash_ready"])
    def test_missing_odex_vdex_must_block(self):
        x={m.JAR:b"newjar",m.BOOTPAIR[0]:b"newart"}
        self.write(self.candidate,x)
        d=m.inspect(self.baseline,self.candidate,True)
        self.assertEqual(d["status"],"BLOCK_MISSING_BOOTCLASSPATH_COMPONENT")
        self.assertEqual(len(d["missing"]),2)
    def test_entire_coherent_pair_stage_not_flash_ready(self):
        x={k:f"new/{i}".encode() for i,k in enumerate(m.PATHS)}
        self.write(self.candidate,x)
        d=m.inspect(self.baseline,self.candidate,True)
        self.assertEqual(d["status"],"ART_CHANGED_REQUIRES_FULL_RUNTIME_ACCEPTANCE")
        self.assertTrue(d["jar_changed"])
        self.assertTrue(d["boot_pair_changed"])
        self.assertFalse(d["flash_ready"])
    def test_identical_staged_jar_not_a_release(self):
        self.write(self.candidate,self.orig)
        d=m.inspect(self.baseline,self.candidate,True)
        self.assertEqual(d["status"],"BLOCK_NO_NEW_TELEPHONY_JAR")
        self.assertFalse(d["jar_changed"])
    def test_missing_jar_must_block(self):
        x={k:v for k,v in self.orig.items() if k!=m.JAR}
        self.write(self.candidate,x)
        self.assertEqual(m.inspect(self.baseline,self.candidate)["status"],
                         "BLOCK_MISSING_BOOTCLASSPATH_COMPONENT")

if __name__=="__main__":unittest.main(verbosity=2)
