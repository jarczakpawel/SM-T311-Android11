#!/usr/bin/env python3
"""G692: fail-closed experimental debugfs no-ref activation + refcount baseline.

The SM-T311 no-ref reader has a conceptual UNTAGGED-old/same-PFN
ABA lifetime gap (G659/G691), therefore debugfs writes of 1 MUST
return -EOPNOTSUPP, even if the test-only config is compiled.
The host-built code may still exist for source/lifetime research.
This script checks real source and compiled ARM32 setter disassembly.
No physical kernel execution, no UAF proof and NO FLASH.
"""
from pathlib import Path
import hashlib,json,re,subprocess

K=Path(__file__).resolve().parents[4]
EXPERIMENT=K.parent/"build/g686-reproducible-exp-on"
RELEASE=K.parent/"build/g687-release-rcu-off"
OUT=K/"Documentation/mm/sm-t311-rcu-interlock-g692.json"
FILES=("mm/filemap.c","mm/page_alloc.c","include/linux/xarray.h",
       "lib/xarray.c","mm/Kconfig","include/linux/uio.h",
       "Documentation/mm/sm-t311-rcu-aba-g659.json")
SOURCE={p:(K/p).read_text() for p in FILES}
def section(s,start,end):
    assert start in s,start
    x=s.split(start,1)[1]; assert end in x,end
    return x.split(end,1)[0]
def ordered(s,*parts):
    pos=[s.find(p) for p in parts]
    return all(k>=0 for k in pos) and pos==sorted(pos) and len(set(pos))==len(pos)
def command(*args):
    return subprocess.run(list(args),text=True,capture_output=True,
                          cwd=K,timeout=42,check=True).stdout

def gates(files):
    fm,pa,xah,xa,kconfig,uio,prior=(files[p] for p in FILES)
    setter=section(fm,"static int lockless_read_set(","DEFINE_DEBUGFS_ATTRIBUTE(")
    lookup=section(fm,"static struct folio *filemap_read_rcu_lookup(",
                   "static size_t filemap_read_rcu_size(")
    get=section(fm,"void *filemap_get_entry(","/**\n * __filemap_get_folio_mpol")
    alloc=section(pa,"static __always_inline bool __free_pages_prepare(",
                   "trace_mm_page_free(page, order);")
    reload=section(xah,"static inline void *xas_reload(", "/**\n * xas_set()")
    modes=json.loads(prior).get("models",{})
    return {
       "debugfs_disabled_activation_experiment_scoped":
           "#ifdef CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT" in setter and
           "return -EOPNOTSUPP;" in setter,
       "safety_interlock_immediately_precedes_legacy_checks":
           ordered(setter,"if (val > 1)","return -EINVAL;",
                   "#ifdef CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT",
                   "if (val)\n\t\treturn -EOPNOTSUPP;",
                   "of_machine_is_compatible(\"samsung,t311\")",
                   "static_branch_enable(&lockless_read_key);"),
       "debugfs_OFF_remains_operable":
           "if (val)\n\t\tstatic_branch_enable(&lockless_read_key);\n\telse\n\t\tstatic_branch_disable(&lockless_read_key);" in setter,
       "invalid_debugfs_value_rejected":
           "if (val > 1)\n\t\treturn -EINVAL;" in setter,
       "single_debugfs_enabler":
           fm.count("static_branch_enable(&lockless_read_key);")==1,
       "disabled_initial_static_key":
           "static DEFINE_STATIC_KEY_FALSE(lockless_read_key);" in fm,
       "G692_abort_errno_is_not_EAGAIN":
           "if (val)\n\t\treturn -EOPNOTSUPP;" in setter,
       "current_marking_needs_key":
           "if (!lockless_read_enabled())" in section(
             fm,"static noinline void filemap_mark_hot(",
             "void filemap_read_accessed("),
       "experimental_read_lookup_needs_key":
           "if (!lockless_read_enabled() ||" in section(
              fm,"size_t filemap_read_rcu(","ssize_t filemap_read("),
       "real_refcounted_lookup_acquires_reference":
           ordered(get,"rcu_read_lock();","folio = xas_load(&xas);",
                   "if (!folio_try_get(folio))",
                   "if (unlikely(folio != xas_reload(&xas)))",
                   "folio_put(folio);","rcu_read_unlock();"),
       "refcounted_lookup_retry_on_invalid_generation":
           "if (!folio_try_get(folio))\n\t\tgoto repeat;" in get and
           "folio_put(folio);\n\t\tgoto repeat;" in get,
       "refcounted_lookup_has_explicit_source_protocol":
           "Lockless page cache protocol:" in fm and
           "Increment the refcount if it's not zero" in fm,
       "xas_reload_exposes_pointer_no_generation":
           "xa_entry(xas->xa, node, offset)" in reload and
           "generation" not in reload,
       "ref_free_candidate_unpinned_before_PG_flag":
           ordered(lookup,"folio = xas_load(xas);",
                   "READ_ONCE(folio->flags.f) & __PG_RCU_FREE",
                   "xas_reload(xas) != folio") and
           "folio_try_get(" not in lookup,
       "only_tagged_last_put_deferred":
           ordered(alloc,"folio_test_rcu_free(folio)",
                   "rcu_free_defer(folio);","return false;"),
       "kconfig_experiment_off_by_default":
           "config SM_T311_RCU_HOT4K_EXPERIMENT" in kconfig and
           "default n" in section(kconfig,
             "config SM_T311_RCU_HOT4K_EXPERIMENT","config PAGE_RCU_FREE"),
       "user_iterator_prior_guard_retained":
           "if (!user_backed_iter(iter))" in fm and
           "return iter_is_ubuf(i) || iter_is_iovec(i);" in uio,
       "four_G659_earlier_herd_diagnostics_retained":
           len(modes)==4 and all(m.get("observation")=="Sometimes"
                              for m in modes.values()),
    }

def altered_sources(original):
    setters={
      "remove_EOPNOTSUPP_interlock":("mm/filemap.c",
            "if (val)\n\t\treturn -EOPNOTSUPP;",
            "if (val)\n\t\t;"),
      "allow_activation_via_inverted_condition":("mm/filemap.c",
            "if (val)\n\t\treturn -EOPNOTSUPP;",
            "if (!val)\n\t\treturn -EOPNOTSUPP;"),
      "allow_activation_by_changing_errno":("mm/filemap.c",
            "return -EOPNOTSUPP;","return 0;"),
      "put_guard_after_enable":("mm/filemap.c",
            "if (val)\n\t\treturn -EOPNOTSUPP;",
            "/* refused after enabling */"),
      "break_OFF_handler":("mm/filemap.c",
            "static_branch_disable(&lockless_read_key);",
            "/* NO disable */"),
      "default_key_ON":("mm/filemap.c",
            "static DEFINE_STATIC_KEY_FALSE(lockless_read_key);",
            "static DEFINE_STATIC_KEY_TRUE(lockless_read_key);"),
      "remove_EINVAL_for_2":("mm/filemap.c",
            "if (val > 1)\n\t\treturn -EINVAL;",
            "if (false)\n\t\treturn -EINVAL;"),
      "skip_reference_acquisition":("mm/filemap.c",
            "if (!folio_try_get(folio))\n\t\tgoto repeat;",
            "if (false)\n\t\tgoto repeat;"),
      "skip_refcounted_XArray_recheck":("mm/filemap.c",
            "if (unlikely(folio != xas_reload(&xas))) {",
            "if (false) {"),
      "skip_XArray_lookup_flag":("mm/filemap.c",
            "READ_ONCE(folio->flags.f) & __PG_RCU_FREE",
            "READ_ONCE(folio->flags.f) & 0"),
      "remove_tagged_allocator_defer":("mm/page_alloc.c",
            "rcu_free_defer(folio);","/* G692 mutant removes defer */"),
      "remove_upstream_G659_LKMM_provenance":(
            "Documentation/mm/sm-t311-rcu-aba-g659.json",
            '"models": {','"models_missing": {'),
    }
    caught={}
    for name,(path,needle,repl) in setters.items():
        assert needle in original[path],name
        changed=dict(original)
        changed[path]=changed[path].replace(needle,repl,1)
        failed=[key for key,ok in gates(changed).items() if not ok]
        assert failed,(name,"source mutation escaped")
        caught[name]=failed
    return caught

def assembly_evidence():
    obj=command("arm-linux-gnueabihf-objdump","-d",
                "--disassemble=lockless_read_set",
                str(EXPERIMENT/"vmlinux"))
    text=obj.split("<lockless_read_set>:",1)[1].split(
         "Disassembly of section .vectors",1)[0]
    nm=command("arm-linux-gnueabihf-nm",str(EXPERIMENT/"vmlinux"))
    # 32-bit EABI func args: (void *data, u64 val): val uses r2/r3.
    # This binary guard checks branch target of nonzero r2/r3 and
    # verifies that target returns -95 by MVN #94 then BX LR.
    branches=re.findall(
        r"(?m)^([0-9a-f]+):\s+[0-9a-f]+\s+bne\s+"
        r"([0-9a-f]+)\s+<lockless_read_set\+0x[0-9a-f]+>",
        text)
    invalid=re.search(r"(?m)^([0-9a-f]+):\s+[0-9a-f]+\s+mvn\s+r0, #94(?:\s+@[^\n]*)?$",text)
    assert invalid,("expected -EOPNOTSUPP instruction missing")
    invalid_addr=int(invalid.group(1),16)
    def jumps_to_invalid(t):
        a=int(t[1],16)
        return a==invalid_addr
    lines=text.splitlines()
    checks={
      "setter_symbol_linked_experiment":
         re.search(r"(?m)^[0-9a-f]+ [Tt] lockless_read_set$",nm) is not None,
      "debugfs_setter_nonzero_dispatch":
         re.search(r"\borrs\s+r2, r2, r3",text) is not None and
         any(jumps_to_invalid(x) for x in branches),
      "nonzero_returns_EOPNOTSUPP_minus95":
         bool(invalid and re.search(
              rf"(?m)^{int(invalid.group(1),16)+4:08x}:.*\bbx\s+lr",text)),
      "invalid_gt1_returns_EINVAL_minus22":
         re.search(r"\bmvn\s+r0, #21",text) is not None,
      "zero_path_does_not_immediately_return_EOPNOTSUPP":
         "orrs\tr2, r2, r3" in text and
         re.search(r"\bbne\s+.*<lockless_read_set\+0x",text) is not None,
      "compiled_arm32_EABI5":
         "ELF 32-bit LSB" in command("file",str(EXPERIMENT/"vmlinux")) and
         "ARM, EABI5" in command("file",str(EXPERIMENT/"vmlinux")),
      "release_off_debugfs_is_not_linked":
         not re.search(r"(?m)^[0-9a-f]+ [Tt] lockless_read_set$",
             command("arm-linux-gnueabihf-nm",str(RELEASE/"vmlinux"))),
    }
    assert len(checks)==7 and all(checks.values()),checks
    negatives={
      "changed_nonzero_branch_destination_detected":
          not any(jumps_to_invalid(x) for x in [
            (addr,hex(invalid_addr+4)[2:]) for addr,targ in branches]),
      "changed_interlock_errno_detected":
          re.search(r"\bmvn\s+r0, #94",text.replace("mvn\tr0, #94","mvn\tr0, #93")) is None,
    }
    assert len(negatives)==2 and all(negatives.values()),negatives
    return checks,negatives

def artifacts():
    result={}
    for label,root in (("experiment_compile_safe_but_ON_refused",EXPERIMENT),
                       ("release_OFF",RELEASE)):
        cfg=(root/".config").read_text()
        assert ("CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y" in cfg)==(
                label=="experiment_compile_safe_but_ON_refused")
        a={}
        for rel in ("vmlinux","arch/arm/boot/zImage","mm/filemap.o"):
            path=root/rel
            assert path.exists(),path
            assert "ARM" in command("file",str(path)),path
            a[rel]={"sha256":hashlib.sha256(path.read_bytes()).hexdigest(),
                    "bytes":path.stat().st_size}
        assert (root/"vmlinux").stat().st_mtime_ns>=(root/"mm/filemap.o").stat().st_mtime_ns
        result[label]=a
    return result

def reproducible_build_provenance():
    scripts=[
       K/"tools/testing/selftests/smt311/build_verify_no_flash_g686.sh",
       K/"tools/testing/selftests/smt311/build_verify_release_off_g687.sh"
    ]
    actual=[p.read_text() for p in scripts]
    expected=(
       'export KBUILD_BUILD_TIMESTAMP="2026-10-21T00:00:00Z"',
       'export KBUILD_BUILD_VERSION=1',
       'export KBUILD_BUILD_USER="smt311"',
       'export KBUILD_BUILD_HOST="reproducible"',
    )
    rules={
       "two_host_build_scripts_same_fixed_provenance":
          all(all(flag in script for flag in expected) for script in actual),
       "build_version_identical_release_and_experiment":
          (EXPERIMENT/"include/generated/utsversion.h").read_text().strip()==
          (RELEASE/"include/generated/utsversion.h").read_text().strip(),
       "fixed_UTS_VERSION_in_both_linked_outputs":
          all('#define UTS_VERSION "#1 SMP PREEMPT 2026-10-21T00:00:00Z"' in
             (root/"include/generated/utsversion.h").read_text()
             for root in (EXPERIMENT,RELEASE)),
       "fixed_kernelrelease_identity":
          all((root/"include/config/kernel.release").read_text().strip()==
             "7.3.0-pjarczak" for root in (EXPERIMENT,RELEASE)),
    }
    assert len(rules)==4 and all(rules.values()),rules
    reject={
       "tamper_experimental_timestamp_detected":
          expected[0] not in actual[0].replace(expected[0],
                     'export KBUILD_BUILD_TIMESTAMP="today"',1),
       "tamper_release_version_detected":
          expected[1] not in actual[1].replace(expected[1],
                     "export KBUILD_BUILD_VERSION=2",1),
    }
    assert all(reject.values()),reject
    return rules,reject

def main():
    positive=gates(SOURCE)
    assert len(positive)==18 and all(positive.values()),positive
    bad=altered_sources(SOURCE)
    assert len(bad)==12
    asm,asm_bad=assembly_evidence()
    binaries=artifacts()
    provenance,provenance_mutants=reproducible_build_provenance()
    report={
      "stage":"G692",
      "result":"Experimental ARM32 debugfs ON is FAIL-CLOSED -EOPNOTSUPP; OFF preserved; no-ref safety unproven",
      "source_conditions":positive,"positive_source_gates":len(positive),
      "altered_C_source_controls_rejected":bad,
      "linked_ARMv7_setter_assembly_conditions":asm,
      "linked_ARMv7_assembly_counterfactuals_detected":asm_bad,
      "reference_acquisition_baseline":"filemap_get_entry: RCU xas_load -> folio_try_get -> xas_reload -> folio_put/retry on mismatch, unlike no-ref reader",
      "linked_host_images":binaries,
      "repeatable_host_build_provenance":provenance,
      "tampered_host_script_provenance_detected":provenance_mutants,
      "double_build_sha256_identities_observed":True,
      "limits":[
        "This is preventive control, not a lifetime/ABA fix; no ref-free ON on the device.",
        "The previously compiled but unreachable pressure/hardware enabling checks remain in test-only C source for later redesigned activation; compiled debugfs setter rejects ON before reaching them.",
        "The emitted ARM32 code is evaluated statically; no physical debugfs write was attempted.",
        "No global proof of PFN generation, VIPT HighMem cache, mmap GP or performance.",
        "The release default RCU-OFF profile is unaffected.",
      ],
      "rcu_ON_allowed":False,
      "boot_installed":False,
      "staged_executable_paths_NOT_INSTALLED":[
         "mm/filemap.c","mm/page_alloc.c","mm/shmem.c","mm/vma.c"]
    }
    OUT.write_text(json.dumps(report,indent=2,ensure_ascii=False)+"\n")
    print("G692",len(positive),"/18 source PASS;",
          len(bad),"/12 actual source mutants REJECTED;",
          len(asm),"/7 ARMv7 setter assembly PASS;",
          len(asm_bad),"/2 assembly mutants detected;",
          len(provenance),"/4 repeatable-build gates PASS;",
          len(provenance_mutants),"/2 script provenance mutants rejected")
    for key,val in binaries.items():
        print(key,{f:x["sha256"] for f,x in val.items()})

if __name__=="__main__":
    main()
