#!/usr/bin/env python3
"""Host selftests for ELF allocated-section equivalence; no live device."""
import importlib.util
from pathlib import Path
import re
import struct
import unittest

path=Path(__file__).resolve().parents[1]/"audit_rild_elf_sections.py"
sp=importlib.util.spec_from_file_location("rildsections",path)
module=importlib.util.module_from_spec(sp)
sp.loader.exec_module(module)
REFERENCE=Path("/home/debian/Projekty/samsung/artifacts/SM-T311-G366-RELEASE-GATE-20261016/g294-live-rild.elf")
REBUILT=Path("/home/debian/Projekty/samsung/artifacts/SM-T311-G366-RELEASE-GATE-20261016/g366-build-rild.elf")

@unittest.skipUnless(REFERENCE.is_file() and REBUILT.is_file(),"private local ELF fixtures absent")
class ElfParityTests(unittest.TestCase):
    def test_known_rild_metadata_difference(self):
        x=module.audit(REFERENCE.read_bytes(),REBUILT.read_bytes())
        self.assertEqual(x["status"],"PASS_RUNTIME_ALLOC_IDENTICAL_BUILD_ID_ONLY")
        self.assertEqual(x["differing_allocated_sections"],[".note.gnu.build-id"])
        self.assertFalse(x["full_file_sha256_equal"])
        self.assertEqual(x["section_count"],23)
        self.assertFalse(x["flash_ready"])
    def test_change_text_is_blocked(self):
        b=bytearray(REBUILT.read_bytes())
        # ELF .text offset 0x14dc for this archived test fixture.
        b[0x14dc]^=0x01
        x=module.audit(REFERENCE.read_bytes(),bytes(b))
        self.assertEqual(x["status"],"BLOCK_ALLOC_ELF_DIFFERENT")
        self.assertIn(".text",x["significant_sections_different"])
    def test_change_data_is_blocked(self):
        b=bytearray(REBUILT.read_bytes())
        b[0x1d70]^=1
        self.assertIn(".data",module.audit(REFERENCE.read_bytes(),bytes(b))["significant_sections_different"])
    def test_bad_elf_rejected(self):
        with self.assertRaises(Exception):module.audit(b"bad",REBUILT.read_bytes())

if __name__=="__main__":unittest.main(verbosity=2)
