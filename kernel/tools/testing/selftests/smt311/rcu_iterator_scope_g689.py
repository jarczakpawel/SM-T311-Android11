#!/usr/bin/env python3
"""G689: real-source-backed iterator whitelist for SM-T311 no-ref RCU read.

The experimental ref-free folio->iov_iter copy is only admitted for
ITER_UBUF/ITER_IOVEC. Kernel ITER_XARRAY/ITER_FOLIOQ/BVEC/KVEC
iterate-and-advance paths are excluded until full ARMv7 lifetime and
cache-alias proofs are available. Does not assert an actual physical
UAF, a universal LKMM proof, or a flashable boot image.
"""
from pathlib import Path
import hashlib, json, re, subprocess

K=Path(__file__).resolve().parents[4]
REL=K.parent/"build/g687-release-rcu-off"
EXP=K.parent/"build/g686-reproducible-exp-on"
OUTPUT=K/"Documentation/mm/sm-t311-rcu-iterator-scope-g689.json"
PATHS=("mm/filemap.c","include/linux/uio.h","include/linux/iov_iter.h",
       "lib/iov_iter.c","mm/vma.c")
SRC={p:(K/p).read_text() for p in PATHS}
TYPES=("ITER_UBUF","ITER_IOVEC","ITER_KVEC","ITER_BVEC","ITER_FOLIOQ",
       "ITER_XARRAY","ITER_DISCARD")

def section(t,a,b):
    assert a in t,a
    v=t.split(a,1)[1]
    assert b in v,b
    return v.split(b,1)[0]

def ordered(t,*needles):
    pos=[t.find(x) for x in needles]
    return min(pos)>=0 and pos==sorted(pos) and len(set(pos))==len(pos)

def gates(inputs):
    fm,uio,iterh,iov,vma=[inputs[x] for x in PATHS]
    func=section(fm,"size_t filemap_read_rcu(", "ssize_t filemap_read(")
    rcbatch=section(fm,"static bool filemap_read_rcu_batch(", "size_t filemap_read_rcu(")
    uback=section(uio,"static inline bool user_backed_iter(", "/*\n * Total number")
    iterator=section(iterh,"size_t iterate_and_advance2(", "/**\n * iterate_and_advance - ")
    xarray=section(iterh,"size_t iterate_xarray(", "/*\n * Handle ITER_DISCARD.")
    folioq=section(iterh,"size_t iterate_folioq(", "/*\n * Handle ITER_XARRAY.")
    cpy=section(fm,"static size_t filemap_read_rcu_copy(", "/* Bytes one RCU read section")
    mmwrite=section(vma,"unsigned long mmap_region(","/**\n * do_brk_flags(")
    return {
      "SMT311_guard_scoped_to_experiment": "#ifdef CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT" in func,
      "ext4_eligibility_before_user_iterator":ordered(func,
          "i_sb->s_magic != EXT4_SUPER_MAGIC",
          "if (!user_backed_iter(iter))","if (!lockless_read_enabled()"),
      "non_user_iterator_early_exit": "if (!user_backed_iter(iter))\n\t\treturn 0;" in func,
      "fastpath_depends_on_mapping_hot_optin": "mapping_rcu_read(iocb->ki_filp->f_mapping)" in func,
      "reader_rechecks_key_inside_rcu":ordered(rcbatch,"rcu_read_lock();",
          "if (unlikely(!lockless_read_enabled()))",
          "mapping_writably_mapped(mapping)", "pagefault_disable();"),
      "source_folio_kmap_and_iter_copy":ordered(cpy,"kmap_local_page(page);",
          "_copy_to_iter(kaddr + offset, len, iter);","kunmap_local(kaddr);"),
      "iov_iter_copier_uses_dispatch": "return iterate_and_advance(i, bytes, (void *)addr," in iov,
      "exact_two_user_backed_types": re.search(r"return\s+iter_is_ubuf\(i\)\s*\|\|\s*iter_is_iovec\(i\)\s*;",uback) is not None and
          "iov_iter_is_bvec(i)" not in uback and "iov_iter_is_xarray(i)" not in uback,
      "seven_known_iterator_types_defined":all(f"\t{t}," in uio for t in TYPES),
      "iterator_dispatch_has_explicit_user_cases":ordered(iterator,
          "iter_is_ubuf(iter)","iter_is_iovec(iter)","iov_iter_is_bvec(iter)",
          "iov_iter_is_kvec(iter)","iov_iter_is_folioq(iter)",
          "iov_iter_is_xarray(iter)","iterate_discard(iter"),
      "xarray_destination_enters_own_rcu":ordered(xarray,
          "rcu_read_lock();","xas_for_each(&xas, folio, ULONG_MAX)",
          "kmap_local_folio(folio, offset);","rcu_read_unlock();"),
      "folioq_destination_maps_extra_folios":ordered(folioq,
          "folioq_folio(folioq, slot)", "kmap_local_folio(folio, skip)",
          "kunmap_local(base)"),
      "ext4_mmap_gp_unchanged": "i_sb->s_magic == EXT4_SUPER_MAGIC" in mmwrite and
          "synchronize_rcu();" in mmwrite,
      "RCU_batch_ends_before_slow_path": "rcu_read_unlock();" in rcbatch and
          "filemap_get_pages(iocb, iter->count, &fbatch, false)" in fm,
      "PAGE_RCU_FREE_OFF_dispatch_direct_zero": "#elif defined(CONFIG_PAGE_RCU_FREE)\n\tfast_read = filemap_read_rcu(iocb, iter, last_pos);\n#else" in fm,
      "no_global_rwsem_added": "down_write(&sm_t311" not in fm and
          "down_write(&sm_t311" not in vma,
    }

def mutants(original):
    mutations={
      "bypass_type_whitelist":("mm/filemap.c",
          "if (!user_backed_iter(iter))", "if (false)"),
      "invert_type_whitelist":("mm/filemap.c",
          "if (!user_backed_iter(iter))", "if (user_backed_iter(iter))"),
      "delete_type_whitelist":("mm/filemap.c",
          "if (!user_backed_iter(iter))\n\t\treturn 0;",
          "/* no type gate */"),
      "allow_bvec_as_user":("include/linux/uio.h",
          "return iter_is_ubuf(i) || iter_is_iovec(i);",
          "return iter_is_ubuf(i) || iter_is_iovec(i) || iov_iter_is_bvec(i);"),
      "allow_xarray_as_user":("include/linux/uio.h",
          "return iter_is_ubuf(i) || iter_is_iovec(i);",
          "return iter_is_ubuf(i) || iov_iter_is_xarray(i);"),
      "skip_inner_static_key":("mm/filemap.c",
          "if (unlikely(!lockless_read_enabled())) {", "if (false) {"),
      "drop_explicit_mmap_gp":("mm/vma.c",
          "synchronize_rcu();","smp_mb();"),
      "remove_xarray_nested_rcu":("include/linux/iov_iter.h",
          "rcu_read_lock();\n\txas_for_each(&xas, folio, ULONG_MAX)",
          "xas_for_each(&xas, folio, ULONG_MAX)"),
      "remove_bounded_source_copy":("mm/filemap.c",
          "_copy_to_iter(kaddr + offset, len, iter);","memcpy(kaddr + offset, kaddr, len);"),
      "remove_marker_eligibility_check":("mm/filemap.c",
          "!mapping_rcu_read(iocb->ki_filp->f_mapping)",
          "false"),
    }
    caught={}
    for name,(path,before,after) in mutations.items():
        assert before in original[path],(name,before)
        inputs=dict(original);inputs[path]=inputs[path].replace(before,after,1)
        failed=[k for k,ok in gates(inputs).items() if not ok]
        assert failed,("mutant escaped",name)
        caught[name]=failed
    return caught

def main():
    positive=gates(SRC)
    assert len(positive)==16 and all(positive.values()),positive
    rejected=mutants(SRC)
    assert len(rejected)==10
    uio=SRC["include/linux/uio.h"]
    whitelist={t: t in ("ITER_UBUF","ITER_IOVEC") for t in TYPES}
    assert all(t in uio for t in whitelist)
    compiled={}
    for name,d in (("release_OFF",REL),("experiment_optional",EXP)):
        config=(d/".config").read_text()
        assert ("CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y" in config)==(
            name=="experiment_optional")
        entries={}
        for rel in ("vmlinux","arch/arm/boot/zImage","mm/filemap.o"):
            path=d/rel
            assert path.exists(),path
            fmt=subprocess.run(["file",str(path)],text=True,capture_output=True,
                               check=True,timeout=12).stdout
            assert "ARM" in fmt,fmt
            entries[rel]={
                "sha256":hashlib.sha256(path.read_bytes()).hexdigest(),
                "size":path.stat().st_size
            }
        assert (d/"vmlinux").stat().st_mtime_ns >= (d/"mm/filemap.o").stat().st_mtime_ns
        assert (d/"arch/arm/boot/zImage").stat().st_mtime_ns >= (d/"vmlinux").stat().st_mtime_ns
        compiled[name]=entries
    report={
       "stage":"G689",
       "scope":"SM-T311 experimental ref-free RCU filemap iterator whitelist source and ARM32 integration",
       "source_anchored_gates":positive,
       "passed":len(positive),
       "actual_changed_source_mutants_rejected":rejected,
       "allowed_iter_types_when_EXPERIMENT=y":whitelist,
       "source_decision":"Only user-backed UBUF and IOVEC may enter experimental RCU; non-user iterators return zero then standard referenced filemap read.",
       "full_ARM32_host_images":compiled,
       "limitations":[
         "Nested XARRAY destination RCU and kmap paths are an audit burden, not a reproduced deadlock or corruption.",
         "Conservative iterator whitelist does not prove PFN ABA safety for user destinations or exclude all indirect RCU/mmap lock cycles.",
         "Normal RCU-OFF release excludes the guard entirely; its runtime unchanged from G688.",
         "Experimental ext4 shared-maywrite mmap GP overhead still exists when static key runtime OFF.",
         "No physical ARMv7 speedup measured; no BOOT package flashed.",
       ],
       "staged_executable_paths_NOT_INSTALLED":["mm/filemap.c","mm/page_alloc.c",
           "mm/shmem.c","mm/vma.c"],
    }
    OUTPUT.write_text(json.dumps(report,indent=2,ensure_ascii=False)+"\n")
    print("G689",len(positive),"/16 real C anchors PASS,",
          len(rejected),"/10 mutated source inputs caught")
    for k,v in compiled.items():
        print(k,{name:data["sha256"] for name,data in v.items()})

if __name__=="__main__":
    main()
