#!/usr/bin/env python3
"""G578: verify the exact 11 prepared G573 ADB slices on the Debian host."""
import hashlib
import json
from pathlib import Path
D = Path("/home/debian/Projekty/samsung/artifacts/SM-T311-G573-FIXED-FINGERPRINT-SYSTEM-20261020")
T = D / "transport64"
m = json.loads((D/"manifest.json").read_text())
s = json.loads((T/"slice_manifest.json").read_text())
assert len(s["files"]) == 11
assert s["zip_sha256"] == m["targetfiles_sha256"]
assert s["raw_sha256"] == m["raw_system_sha256"]
for part in m["parts"]:
    h = hashlib.sha256()
    items = sorted((x for x in s["files"] if x["part"] == part["part"]),key=lambda x:x["slice"])
    assert len(items) >= 1
    assert [x["slice"] for x in items] == list(range(len(items)))
    n = 0
    for x in items:
        assert x["name"] == f'chunk{part["part"]}.gz.part{x["slice"]:02d}'
        assert 0 < x["size"] <= 64*1024*1024
        payload = (T/x["name"]).read_bytes()
        assert len(payload) == x["size"]
        assert hashlib.sha256(payload).hexdigest() == x["sha256"]
        h.update(payload);n += len(payload)
    assert h.hexdigest() == part["gzip_sha256"]
    assert n == part["gzip_bytes"]
    print("G578_G573_CHUNK",part["part"],"SLICES",len(items),"TRANSFER_SHA_PASS")
assert sum(x["size"] for x in s["files"]) == sum(x["gzip_bytes"] for x in m["parts"])
print("G578_G573_11_SLICES_INTEGRITY_PASS")
print("G578_NO_ADB_OR_FLASH")
