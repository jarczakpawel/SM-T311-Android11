#!/usr/bin/env python3
"""Offline unit tests for G366 fail-closed target-files release checker."""
import hashlib
import io
import struct
import tempfile
import unittest
import warnings
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED

import audit_target_files_vs_live as audit

class TargetFilesAuditTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="g366-fixture-")
        self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name)
        container=io.BytesIO()
        with ZipFile(container,"w",ZIP_DEFLATED) as f:
            f.writestr("classes.dex",b"dex\n035\x00" + b"fakedex\x00"*17)
        self.apk=container.getvalue()
        with ZipFile(io.BytesIO(self.apk)) as z:
            checksum=z.getinfo("classes.dex").CRC
        self.vdex=b"vdex021\x00"+b"000\x00"+struct.pack("<IIIII",1,0,0,0,checksum)
        self.data={
            "/vendor/lib/libril.so":b"ril",
            "/vendor/lib/t311-ril/libsec-ril.so":b"oem",
            "/vendor/lib/hw/gps.default.so":b"gps",
            "/sepolicy":b"policy",
            "/system/system_ext/priv-app/SystemUI/SystemUI.apk":self.apk,
            "/system/system_ext/priv-app/SystemUI/oat/arm/SystemUI.odex":b"elfodata",
            "/system/system_ext/priv-app/SystemUI/oat/arm/SystemUI.vdex":self.vdex,
        }
        self.boot=b"\0"*(3339*2048)
        self.live=self.root/"live.txt"
        self.live.write_text("".join(hashlib.sha256(v).hexdigest()+"  "+k+"\n" for k,v in self.data.items()))
        self.zip=self.root/"target.zip"

    def create_zip(self,changes=None,omit=(),duplicates=False,boot=True):
        changes=changes or {}
        with ZipFile(self.zip,"w",ZIP_DEFLATED) as z:
            for k,v in self.data.items():
                if k not in omit:
                    z.writestr(audit.mapping(k)[0],changes.get(k,v))
            if boot:
                z.writestr("IMAGES/boot.img",self.boot)
            if duplicates:
                with warnings.catch_warnings():
                    warnings.simplefilter("ignore")
                    z.writestr(audit.mapping("/vendor/lib/libril.so")[0],b"duplicated")
    def test_complete_pass(self):
        self.create_zip()
        r=audit.check(self.zip,self.live,hashlib.sha256(self.boot).hexdigest())
        self.assertEqual(r["status"],"PASS")
        self.assertEqual(r["systemui_preopt"]["status"],"MATCH")
        self.assertEqual(r["boot_prefix"]["status"],"MATCH")
    def test_vendor_mismatch_blocks(self):
        self.create_zip({"/vendor/lib/hw/gps.default.so":b"tampered"})
        r=audit.check(self.zip,self.live)
        self.assertEqual(r["critical"]["/vendor/lib/hw/gps.default.so"],"DIFFERENT")
        self.assertEqual(r["status"],"BLOCK")
    def test_vdex_mismatch_blocks(self):
        tampered=bytearray(self.vdex)
        tampered[-1]^=1
        self.create_zip({"/system/system_ext/priv-app/SystemUI/oat/arm/SystemUI.vdex":bytes(tampered)})
        r=audit.check(self.zip,self.live)
        self.assertEqual(r["systemui_preopt"]["status"],"MISMATCH")
        self.assertEqual(r["status"],"BLOCK")
    def test_malformed_vdex_blocks(self):
        self.create_zip({"/system/system_ext/priv-app/SystemUI/oat/arm/SystemUI.vdex":b"bad"})
        r=audit.check(self.zip,self.live)
        self.assertEqual(r["systemui_preopt"]["status"],"INVALID")
        self.assertEqual(r["status"],"BLOCK")
    def test_boot_mismatch_blocks(self):
        self.create_zip()
        r=audit.check(self.zip,self.live,"f"*64)
        self.assertEqual(r["boot_prefix"]["status"],"DIFFERENT")
        self.assertEqual(r["status"],"BLOCK")
    def test_missing_file_blocks(self):
        self.create_zip(omit=("/vendor/lib/libril.so",))
        r=audit.check(self.zip,self.live)
        self.assertEqual(r["critical"]["/vendor/lib/libril.so"],"MISSING")
        self.assertEqual(r["status"],"BLOCK")
    def test_duplicate_file_rejected(self):
        self.create_zip(duplicates=True)
        with self.assertRaisesRegex(ValueError,"Duplicate ZIP paths"):
            audit.check(self.zip,self.live)
    def test_incomplete_live_rejected(self):
        self.live.write_text("")
        self.create_zip()
        with self.assertRaisesRegex(ValueError,"Incomplete live snapshot"):
            audit.check(self.zip,self.live)
    def test_no_boot_image_blocks_if_requested(self):
        self.create_zip(boot=False)
        r=audit.check(self.zip,self.live,hashlib.sha256(self.boot).hexdigest())
        self.assertEqual(r["boot_prefix"]["status"],"MISSING")
        self.assertEqual(r["status"],"BLOCK")

if __name__=="__main__":
    unittest.main(verbosity=2)
