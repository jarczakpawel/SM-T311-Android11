#!/usr/bin/env python3
"""SM-T311 G654: fail-closed LKMM preparation and source-order checker.

--lint is a source/layout preflight, NOT a formal memory-model result.
--run executes actual herd7 with the kernel-provided linux-kernel.cfg.
If herd7 is absent, --run exits 2 instead of pretending the tests passed.
The outcomes are *not* used to automatically enable unsafe ref-free RCU.
"""
import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

KERNEL = Path(__file__).resolve().parents[4]
MODEL = KERNEL / "tools/memory-model"
PREFIX = "SMT311+"
TESTS = {
    "SMT311+PubWmbRmb+FlagData.litmus": "generic message-passing positive control",
    "SMT311+NoWmb+FlagData.litmus": "deliberately weakened publisher negative control",
    "SMT311+AddrABA+FlagSizeReload.litmus": "abstract same-pointer generation-ABA risk explorer",
    "SMT311+IsizeSeqcount+32Smp.litmus": "ARMv7 32bit seqcount-protected split i_size abstract model",
    "SMT311+IsizeNoSeqRetry+32Smp.litmus": "deliberately broken no-retry 64-bit torn-read control",
    "SMT311+RCUGraceLastFree.litmus": "RCU grace before old folio contents recycled, real synchronize_rcu abstract model",
    "SMT311+NoRCUGraceLastFree.litmus": "negative control: reuse old memory without grace after slot update"
}


def source_gate():
    filemap = (KERNEL / "mm/filemap.c").read_text()
    fs_header = (KERNEL / "include/linux/fs.h").read_text()
    config = KERNEL / "tools/testing/selftests/smt311"
    g650 = (config / "plan_check.py").read_text()
    chunk = filemap.split("static struct folio *filemap_read_rcu_lookup(", 1)[1]
    chunk = chunk.split("static size_t filemap_read_rcu_size(", 1)[0]
    anchors = [
        "xas_load(xas)",
        "READ_ONCE(folio->flags.f) & __PG_RCU_FREE",
        "smp_rmb();",
        "*isize = i_size_read(inode);",
        "xas_reload(xas) != folio",
        "folio_zonenum(folio) != ZONE_NORMAL",
        "READ_ONCE(folio->mapping) != mapping"
    ]
    positions = [chunk.index(key) for key in anchors]
    assert positions == sorted(positions), "RCU read ordering unexpectedly changed"
    i_size = fs_header.split("static inline loff_t i_size_read(", 1)[1].split(
        "static inline void i_size_write(", 1)[0]
    i_write = fs_header.split("static inline void i_size_write(", 1)[1].split(
        "static inline unsigned iminor(", 1)[0]
    must = {
        "source_i_size_32bit_SMP_seqcount":
          "#if BITS_PER_LONG==32 && defined(CONFIG_SMP)" in i_size
          and "read_seqcount_begin(&inode->i_size_seqcount)" in i_size
          and "read_seqcount_retry(&inode->i_size_seqcount, seq)" in i_size,
        "source_i_size_32bit_SMP_writer_seqcount":
          "#if BITS_PER_LONG==32 && defined(CONFIG_SMP)" in i_write
          and "write_seqcount_begin(&inode->i_size_seqcount)" in i_write
          and "write_seqcount_end(&inode->i_size_seqcount)" in i_write
          and "preempt_disable();" in i_write,
        "source_rcu_read_flag_size_slot_mapping_order": True,
        "source_G650_disabled_hot_marker_still_skipped":
          "if (hot && lockless_read_enabled())" in filemap,
        "source_highmem_no_ref_excluded":
          "folio_zonenum(folio) != ZONE_NORMAL" in chunk,
        "release_status_contract_kept":
          "runtime_directories" in g650
    }
    assert all(must.values()), must
    return must


def lint():
    result = source_gate()
    for name in TESTS:
        text = (MODEL / "litmus-tests" / name).read_text()
        assert text.startswith("C " + name.removesuffix(".litmus") + "\n"), name
        assert "P0(" in text and "P1(" in text and "exists (" in text, name
        assert "READ_ONCE(" in text and "WRITE_ONCE(" in text, name
        assert text.count("{") == text.count("}"), name
    return {
      "board": "Samsung SM-T311 Cortex-A9 32-bit SMP",
      "model_syntax_status": "STATIC PREFLIGHT ONLY, NOT herd7 parsed",
      "source_gates": result,
      "models": TESTS,
      "herd7_available": bool(shutil.which("herd7")),
      "safety_decision": "DO NOT ENABLE experimental RCU ON; S2/S3 BLOCKED"
    }


def herd_smoke(timeout):
    """Require actual LKMM interpretation with local kernel model resources.

    Some herdtools7 builds lack system-wide .cat model resources. Always
    use -I <kernel>/tools/memory-model with the SAME-tree cfg/def/cat/bell
    files, and validate the upstream full-fence SB observation "Never".
    """
    herd = shutil.which("herd7")
    if herd is None:
        print("G655 herd7 missing: NO FORMAL MODEL EXECUTION", file=sys.stderr)
        return 2
    file = MODEL / "litmus-tests/SB+fencembonceonces.litmus"
    proc = subprocess.run(
        [herd, "-I", str(MODEL), "-conf", str(MODEL / "linux-kernel.cfg"), str(file)],
        capture_output=True, text=True, timeout=timeout, check=False
    )
    if proc.returncode:
        print("G655 herd7 smoke failure, including potentially missing "
              "model resources: " + proc.stderr[-1000:], file=sys.stderr)
        return 3
    # herd7's documented Linux outcome is 'Observation ... Never 0 N'.
    if not re.search(r"Observation\s+SB\+fencembonceonces\s+Never\s+0\s+\d+", proc.stdout):
        print("G655 herd7 did not reproduce kernel memory model smoke Never outcome. "
              "NO LKMM ACCEPTANCE: " + proc.stdout[-900:], file=sys.stderr)
        return 4
    print("G655 LKMM SMOKE PASS: upstream full fence SB disallowed under linux-kernel.cfg")
    return 0


# Only structural expectations for intentionally simplified demonstration
# models. These are NOT formal safety acceptance criteria for the kernel.
EXPECTED_OBSERVATION = {
    "SMT311+PubWmbRmb+FlagData.litmus": "Never",
    "SMT311+NoWmb+FlagData.litmus": "Sometimes",
    "SMT311+AddrABA+FlagSizeReload.litmus": "Sometimes",
    "SMT311+IsizeSeqcount+32Smp.litmus": "Never",
    "SMT311+IsizeNoSeqRetry+32Smp.litmus": "Sometimes",
    "SMT311+RCUGraceLastFree.litmus": "Never",
    "SMT311+NoRCUGraceLastFree.litmus": "Sometimes",
}


def parse_observation(output, name):
    base = name.removesuffix(".litmus")
    matching = re.findall(
        r"^Observation\s+"+re.escape(base)+r"\s+(Never|Sometimes|Always)\s+(\d+)\s+(\d+)\s*$",
        output, re.M)
    if len(matching) != 1:
        raise ValueError("missing or ambiguous herd7 observation: " + name)
    kind, positive, negative = matching[0]
    p, n = int(positive), int(negative)
    if p+n == 0 or ((kind == "Never") != (p == 0)):
        raise ValueError("inconsistent herd7 witness accounting: " + name)
    return {"observation":kind,"positive_witnesses":p,
            "negative_witnesses":n,"total_candidates":p+n}


def run_herd(timeout):
    status = herd_smoke(timeout)
    if status:
        return status
    herd = shutil.which("herd7")
    if set(TESTS) != set(EXPECTED_OBSERVATION):
        print("G658 test set is not fully covered by expectation checks", file=sys.stderr)
        return 6
    results = {}
    for file in TESTS:
        args = [herd, "-I", str(MODEL), "-conf", str(MODEL / "linux-kernel.cfg"),
                str(MODEL / "litmus-tests" / file)]
        proc = subprocess.run(args, capture_output=True, text=True,
                              timeout=timeout, check=False)
        print("=== MODEL", file, "exit", proc.returncode, "===")
        print(proc.stdout[-3000:])
        if proc.stderr:
            print("STDERR", proc.stderr[-1500:], file=sys.stderr)
        if proc.returncode:
            return 3
        try:
            result = parse_observation(proc.stdout,file)
        except ValueError as exc:
            print("G657 INVALID LKMM OUTPUT:",exc,file=sys.stderr)
            return 4
        if result["observation"] != EXPECTED_OBSERVATION[file]:
            print("G657 UNEXPECTED LKMM OUTCOME",file,result,file=sys.stderr)
            return 5
        results[file] = result
    print("G657_LKMM_VERIFIED_RESULTS_JSON_BEGIN")
    print(json.dumps(results,ensure_ascii=False,indent=2))
    print("G657_LKMM_VERIFIED_RESULTS_JSON_END")
    print(f"G658 all {len(TESTS)} LKMM models executed against local kernel model files.")
    print("This does NOT certify ref-free RCU lifetime; S2 and S3 remain BLOCKED.")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--lint", action="store_true", help="static preflight only")
    ap.add_argument("--run", action="store_true", help="run upstream smoke and local examples using real herd7")
    ap.add_argument("--probe", action="store_true", help="verify a real herd7 installation and LKMM model files")
    ap.add_argument("--timeout", type=int, default=90)
    args = ap.parse_args()
    status = lint()
    print(json.dumps(status, indent=2))
    if args.probe:
        return herd_smoke(args.timeout)
    if args.run:
        return run_herd(args.timeout)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
