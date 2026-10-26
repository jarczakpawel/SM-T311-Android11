#!/usr/bin/env python3
"""Optional host-side stock Samsung RIL incoming-call ABI test.

Requires a separately supplied proprietary binary. CI and public GitHub clones
can run without proprietary blobs; in --require-blob mode a missing file fails.
No Samsung binary is bundled, displayed, copied, or published.
"""
import argparse
from pathlib import Path
import re
import subprocess

parser=argparse.ArgumentParser()
parser.add_argument("--blob",type=Path,help="Private Samsung OEM libsec-ril.so path")
parser.add_argument("--require-blob",action="store_true",help="Fail rather than skip missing OEM")
args=parser.parse_args()
if args.blob is None or not args.blob.is_file():
    if args.require_blob:
        raise SystemExit("G369_STOCK_CALL_TEST_FAIL: --blob OEM library required")
    print("G369_STOCK_CALL_TEST_SKIP: OEM binary unavailable (not redistributed)")
    raise SystemExit(0)

blob=args.blob
readelf=subprocess.check_output(["readelf","-W","-s",str(blob)],text=True)
symbol="_ZN11CallManager14OnCallIncomingEv"
match=re.search(r"^\s*\d+: ([0-9a-f]+)\s+(\d+)\s+FUNC\s+GLOBAL\s+DEFAULT\s+\d+\s+"+symbol+r"$",readelf,re.M)
assert match,"Stock function missing"
offset=int(match.group(1),16)&~1
length=int(match.group(2))
assert length>30
decoded=subprocess.check_output(["llvm-objdump","--disassemble","--triple=thumbv7-linux-gnueabi",
    f"--start-address={offset}",f"--stop-address={offset+length}",str(blob)],text=True)
assert re.search(r"movw\s+r1,\s+#0x3fa",decoded),"CALL_RING 1018 missing"
assert re.search(r"movw\s+r1,\s+#0x3e9",decoded),"CALL_STATE_CHANGED 1001 missing"
assert decoded.count("UnsolicitedResponse")>=2,"OEM does not forward both"
assert decoded.index("#0x3fa")<decoded.index("#0x3e9"),"Wrong stock event order"
print("G369_OEM_STOCK_INCOMING_PASS: 1018 CALL_RING then 1001 CALL_STATE_CHANGED")
