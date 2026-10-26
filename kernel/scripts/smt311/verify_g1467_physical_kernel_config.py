#!/usr/bin/env python3
import argparse
import hashlib
import json
from pathlib import Path

BASE = Path(__file__).resolve().parents[2] / "arch/arm/configs"
EXPECTED_SHA = "ca7d1c72025b012c34dca79ff65b234ab7e0b6b154bf04f7aaa4c06b0752c988"

def symbols(path):
    result = {}
    for line in path.read_text().splitlines():
        if line.startswith("CONFIG_"):
            key = line.split("=", 1)[0]
        elif line.startswith("# CONFIG_") and line.endswith(" is not set"):
            key = line[2:-len(" is not set")]
        else:
            continue
        if key in result:
            raise ValueError("duplicate " + key)
        result[key] = line
    return result

def delta(a, b):
    return {k: (a.get(k), b.get(k)) for k in a.keys() | b.keys() if a.get(k) != b.get(k)}

parser = argparse.ArgumentParser()
parser.add_argument("--generated-production", type=Path)
parser.add_argument("--generated-candidate", type=Path)
args = parser.parse_args()
ref = BASE / "sm_t311_android11_g1441_physical_defconfig"
original = BASE / "sm_t311_android11_defconfig"
candidate = BASE / "sm_t311_android11_g1441_hz300_experiment_defconfig"
if hashlib.sha256(ref.read_bytes()).hexdigest() != EXPECTED_SHA:
    raise SystemExit("G1467_PHYSICAL_SOURCE_SHA_INVALID")
a, b, c = (symbols(p) for p in (ref, original, candidate))
assert len(a) == len(b) == len(c) == 5156
assert not delta(a, b), "G1467_PRODUCTION_NOT_PHYSICAL_EXACT"
expected = {
    "CONFIG_HZ": ("CONFIG_HZ=200", "CONFIG_HZ=300"),
    "CONFIG_HZ_200": ("CONFIG_HZ_200=y", "# CONFIG_HZ_200 is not set"),
    "CONFIG_HZ_300": ("# CONFIG_HZ_300 is not set", "CONFIG_HZ_300=y"),
}
assert delta(a, c) == expected, "G1467_HZ300_UNQUALIFIED_SYMBOL_DIFFERENCES"
for k, v in {
    "CONFIG_CHARGER_MAX77693_T311_OTG_EXPERIMENTAL": "y",
    "CONFIG_DEBUG_FS": "y",
    "CONFIG_PREEMPT": "y",
    "CONFIG_HZ": "200",
    "CONFIG_LOCALVERSION": '"-pjarczak"',
}.items():
    assert a[k] == f"{k}={v}"
if args.generated_production:
    assert not delta(a, symbols(args.generated_production))
if args.generated_candidate:
    assert not delta(c, symbols(args.generated_candidate))
print(json.dumps({
    "status": "PASS",
    "symbols": len(a),
    "physical_sha256": EXPECTED_SHA,
    "production_exact": True,
    "candidate_only_delta": sorted(expected),
    "host_generated_production_checked": bool(args.generated_production),
    "host_generated_candidate_checked": bool(args.generated_candidate),
    "experimental_kernel_installed": False
}, sort_keys=True, indent=2))
