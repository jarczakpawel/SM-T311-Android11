#!/usr/bin/env python3
"""G376 host-only real G375 QA fixtures plus negative tamper cases."""
from copy import deepcopy
import importlib.util
import hashlib
import json
import tempfile
import zipfile
from pathlib import Path
import unittest

source=Path(__file__).resolve().parents[1]/"g376_release_gate.py"
spec=importlib.util.spec_from_file_location("releasegate",source)
gate=importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)
work=Path("/home/debian/Projekty/samsung/artifacts/SM-T311-G374-CARRIER-READY-20261016")
fixture_paths=[
    work/"G375-REBUILT-RELEASE-AUDIT.json",
    work/"G375-RILD-RUNTIME-SECTIONS-PARITY.json",
    work/"G375-FULL-RELEASE-INTEGRATION-REPORT.json",
]

@unittest.skipUnless(all(p.is_file() for p in fixture_paths),"private G375 release audit files absent")
class StockParityReleaseTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture=[json.loads(p.read_text()) for p in fixture_paths]
    def setUp(self):
        self.a,self.r,self.m=deepcopy(self.fixture)
    def check(self):
        return gate.evaluate(self.a,self.r,self.m)
    def test_baseline_rejects_four_permissive(self):
        r=self.check();self.assertEqual(r["status"],"HOLD_SELINUX")
        self.assertEqual((r["passed"],r["total"]),(10,11))
        self.assertFalse(r["flash_ready"])
    def test_unexpected_bypass_blocked(self):
        self.a["comparison"]["/vendor/lib/libril.so"]["status"]="DIFFERENT"
        r=self.check()
        self.assertEqual(r["status"],"BLOCK_OTHER_RELEASE_GATES")
        self.assertIn("/vendor/lib/libril.so",r["unexpected_changed_paths"])
    def test_systemui_preopt_stale_blocked(self):
        self.a["systemui_preopt"]["vdex_dex_crc32"]=["deadbeef"]
        self.assertIn("systemui_pair_crc",self.check()["blocking_reasons"])
    def test_rild_text_changed_blocked(self):
        self.r["significant_sections_different"]=[".text"]
        self.assertIn("rild_code_data_metadata_only",self.check()["blocking_reasons"])
    def test_boot_mismatch_blocked(self):
        self.a["boot_prefix"]["target_sha256"]="0"*64
        self.assertIn("physical_boot_match",self.check()["blocking_reasons"])
    def test_recovery_mismatch_blocked(self):
        self.m["critical_files"]["BOOTABLE_IMAGES/recovery.img"]["sha256"]="0"*64
        self.assertIn("physical_recovery_match",self.check()["blocking_reasons"])
    def test_carrier_protected_ril_changed_blocked(self):
        self.m["critical_files"]["SYSTEM/vendor/lib/libril.so"]["sha256"]="0"*64
        self.assertIn("safe_RIL_G368_match",self.check()["blocking_reasons"])
    def test_missing_file_blocked(self):
        self.a["comparison"]["/vendor/lib/libril.so"]["status"]="MISSING"
        self.assertIn("/vendor/lib/libril.so",self.check()["missing"])
    def test_unknown_permissive_domain_blocked(self):
        self.a["selinux_policy"]["permissive_domains"].append("untrusted_app")
        self.assertIn("no_unexpected_policy_domains",self.check()["blocking_reasons"])
    def test_all_enforcing_still_requires_live_acceptance(self):
        self.a["selinux_policy"]["permissive_domains"]=[]
        self.m["flash_ready"]=False
        r=self.check()
        self.assertEqual(r["status"],"NEEDS_LIVE_PHYSICAL_ACCEPTANCE")
        self.assertFalse(r["flash_ready"])
        self.assertEqual(r["blocking_reasons"],[])
    def test_no_bypass_by_manifest_flash_ready(self):
        self.m["flash_ready"]=True
        self.assertIn("integrated_targetfiles_manifest_not_flash_ready",self.check()["blocking_reasons"])

class TargetFilesBytesTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory()
        self.zip_path=Path(self.tmp.name)/"release.zip"
        self.payload={f"SYSTEM/file-{i:02}.txt":f"G376-{i}".encode() for i in range(17)}
        with zipfile.ZipFile(self.zip_path,"w",zipfile.ZIP_DEFLATED) as z:
            for k,v in self.payload.items():z.writestr(k,v)
        self.report={
            "targetfiles_sha256":hashlib.sha256(self.zip_path.read_bytes()).hexdigest(),
            "critical_files":{k:{"sha256":hashlib.sha256(v).hexdigest(),"bytes":len(v)}
                              for k,v in self.payload.items()},
        }
    def tearDown(self):self.tmp.cleanup()
    def test_actual_zip_17_files_match(self):
        d=gate.verify_target_files_bytes(self.zip_path,self.report)
        self.assertTrue(d["byte_integrity_ok"])
        self.assertEqual(d["critical_payloads_matched"],17)
    def test_report_forged_zip_sha_rejected(self):
        self.report["targetfiles_sha256"]="0"*64
        self.assertFalse(gate.verify_target_files_bytes(self.zip_path,self.report)["byte_integrity_ok"])
    def test_report_forged_file_sha_rejected(self):
        self.report["critical_files"]["SYSTEM/file-05.txt"]["sha256"]="0"*64
        d=gate.verify_target_files_bytes(self.zip_path,self.report)
        self.assertFalse(d["byte_integrity_ok"])
        self.assertEqual(d["critical_payload_discrepancies"],["SYSTEM/file-05.txt"])
    def test_deleted_file_rejected(self):
        self.report["critical_files"]["SYSTEM/missing.txt"]={"sha256":"0"*64,"bytes":0}
        self.assertFalse(gate.verify_target_files_bytes(self.zip_path,self.report)["byte_integrity_ok"])

if __name__=="__main__":unittest.main(verbosity=2)
