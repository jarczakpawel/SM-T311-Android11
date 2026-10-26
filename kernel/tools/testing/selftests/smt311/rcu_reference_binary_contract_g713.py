#!/usr/bin/env python3
"""G713: verify the *linked* SM-T311 RCU-OFF production read ABI.

Read-only build gate for existing G686/G700 ARMv7 host ELF images.
Not a guarantee about untested subsystems or the running G650 device.
"""
from pathlib import Path
from hashlib import sha256
import json
import re
import subprocess

K = Path(__file__).resolve().parents[4]
P = K.parent
PATHS = {
    "experiment": P / "build/g686-reproducible-exp-on",
    "release": P / "build/g700-debugfs-rcu-off",
}
SOURCES = {
    "filemap": (K / "mm/filemap.c").read_text(),
    "shmem": (K / "mm/shmem.c").read_text(),
    "config": (PATHS["release"] / ".config").read_text(),
}

def command(args):
    return subprocess.run(args, text=True, capture_output=True, check=True).stdout

def symbols(path):
    result = {}
    for line in command(["arm-linux-gnueabihf-nm", "-S", str(path)]).splitlines():
        m = re.match(r"^([0-9a-f]+)\s+([0-9a-f]+)\s+([tT])\s+(\w+)$", line)
        if m and m.group(4) in {
            "filemap_read_rcu", "filemap_read", "shmem_file_read_iter",
            "filemap_get_pages", "filemap_get_read_batch", "filemap_get_entry",
        }:
            result[m.group(4)] = {"address": m.group(1), "size": int(m.group(2), 16)}
    return result

def disassemble(path, name):
    raw = command(["arm-linux-gnueabihf-objdump", "-d",
                   "--disassemble=" + name, str(path)])
    # objdump also prints empty section headers; isolate function body only.
    begin = re.search(r"(?m)^[0-9a-f]+ <" + re.escape(name) + r">:\s*$", raw)
    if not begin:
        raise RuntimeError("Missing linked function: " + name)
    result = []
    for line in raw[begin.end():].splitlines():
        if not line.strip():
            if result:
                break
            continue
        match = re.search(
            r"^\s*([0-9a-f]+):\s+([0-9a-f]{8})\s+([a-z][a-z0-9.]*)\s*(.*)$",
            line
        )
        if match:
            result.append({"pc": match.group(1), "word": match.group(2),
                           "op": match.group(3), "operand": match.group(4)})
        elif result:
            break
    if not result:
        raise RuntimeError("Could not parse instructions: " + name)
    return result

def joined(instructions):
    return "\n".join(
        " ".join((v["word"],v["op"],v["operand"])) for v in instructions
    )

def function_body(text, name, next_marker):
    start=text.index(name)
    stop=text.index(next_marker, start)
    return text[start:stop]

build = {k: path / "vmlinux" for k, path in PATHS.items()}
sym = {name: symbols(path) for name, path in build.items()}
arch = command(["arm-linux-gnueabihf-readelf", "-h", str(build["release"])])
dasm = {}
for cfg in build:
    dasm[cfg] = {name: disassemble(build[cfg], name) for name in (
        "filemap_read_rcu", "filemap_read", "shmem_file_read_iter",
        "filemap_get_pages", "filemap_get_read_batch", "filemap_get_entry"
    )}

def checks_for(source=SOURCES, binary=dasm):
    f = source["filemap"]
    sh = source["shmem"]
    conf = source["config"]
    release = binary["release"]
    experimental = binary["experiment"]
    batch = function_body(f, "static void filemap_get_read_batch(", "\nstatic int filemap_read_folio(")
    entry = function_body(f, "void *filemap_get_entry(", "\nstruct folio *__filemap_get_folio_mpol(")
    main = function_body(f, "ssize_t filemap_read(", "\nint kiocb_write_and_wait(")
    shread = function_body(sh, "static ssize_t shmem_file_read_iter(", "\nstatic")
    main_off = main.split("#elif defined(CONFIG_PAGE_RCU_FREE)", 1)[1].split("#endif",1)[0].split("#else",1)[1]
    shread_off = shread.split("#elif defined(CONFIG_PAGE_RCU_FREE)",1)[1].split("#endif",1)[0].split("#else",1)[1]
    main_off = re.sub(r"/\*.*?\*/", "", main_off, flags=re.S)
    shread_off = re.sub(r"/\*.*?\*/", "", shread_off, flags=re.S)
    mainasm = joined(release["filemap_read"])
    sham = joined(release["shmem_file_read_iter"])
    batchasm = joined(release["filemap_get_read_batch"])
    expmain = joined(experimental["filemap_read"])
    stub = release["filemap_read_rcu"]
    return {
        "host_release_ELF32_ARM": "ELF32" in arch and "Machine:                           ARM" in arch,
        "release_CONFIG_FUSE_unrelated_RCU_OFF": (
            "CONFIG_FUSE_FS=y" in conf
            and "CONFIG_PAGE_RCU_FREE=y" not in conf
            and "CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y" not in conf
        ),
        "release_ref_free_stub_linked_8_bytes": sym["release"]["filemap_read_rcu"]["size"] == 8,
        "release_stub_is_exact_mov_zero_bx_lr": (
            len(stub) == 2
            and stub[0]["op"] == "mov"
            and stub[0]["operand"].replace(" ","") == "r0,#0"
            and stub[1]["op"] == "bx"
            and stub[1]["operand"] == "lr"
        ),
        "filemap_read_G700_does_not_branch_to_unref": "<filemap_read_rcu>" not in mainasm,
        "shmem_read_G700_does_not_branch_to_unref": "<filemap_read_rcu>" not in sham,
        "filemap_read_G686_calls_real_unref": "<filemap_read_rcu>" in expmain,
        "filemap_read_G686_unref_is_real_function": sym["experiment"]["filemap_read_rcu"]["size"] > 1000,
        "filemap_read_G700_uses_refcounted_page_provider": "<filemap_get_pages>" in mainasm,
        "filemap_get_pages_G700_calls_reference_batch": "<filemap_get_read_batch>" in joined(release["filemap_get_pages"]),
        "filemap_get_read_batch_has_tryget_under_RCU": (
            batch.index("rcu_read_lock();") <
            batch.index("folio_try_get(folio)") <
            batch.index("xas_reload(&xas)") <
            batch.index("rcu_read_unlock();")
        ),
        "filemap_get_read_batch_retry_drops_reference": (
            "folio_put(folio);" in batch and "xas_reset(&xas);" in batch
        ),
        "release_reference_batch_linked_nonempty": sym["release"]["filemap_get_read_batch"]["size"] >= 256,
        "release_reference_batch_has_ARM32_unpin_branch": (
            "<__folio_put>" in batchasm or "folio_put" in batchasm
        ),
        "filemap_get_entry_RCU_tryget_reload_put": (
            entry.index("rcu_read_lock();") <
            entry.index("folio_try_get(folio)") <
            entry.index("xas_reload(&xas)") <
            entry.index("folio_put(folio);") <
            entry.index("rcu_read_unlock();")
        ),
        "filemap_read_copy_keeps_folio_until_put": (
            main.index("filemap_get_pages(iocb, iter->count") <
            main.index("copy_folio_to_iter(folio") <
            main.index("folio_put(folio);")
        ),
        "filemap_read_preserves_alias_flush": (
            "mapping_writably_mapped(mapping)" in main
            and main.index("flush_dcache_folio(folio)") < main.index("copy_folio_to_iter(folio")
        ),
        "shmem_read_after_skip_enters_normal_loop": (
            "retval = 0;" in shread
            and "for (;;) {" in shread
            and shread.index("retval = 0;") < shread.index("for (;;) {")
        ),
        "source_release_filemap_skips_unref_dispatch": (
            "#elif defined(CONFIG_PAGE_RCU_FREE)" in main
            and "fast_read = 0;" in main_off
            and "filemap_read_rcu(" not in main_off
        ),
        "source_release_shmem_skips_unref_dispatch": (
            "#elif defined(CONFIG_PAGE_RCU_FREE)" in shread
            and "retval = 0;" in shread_off
            and "filemap_read_rcu(" not in shread_off
        ),
        "still_no_ref_free_activation_in_staged_G692": (
            "if (val)\n\t\treturn -EOPNOTSUPP;" in f
            and "static DEFINE_STATIC_KEY_FALSE(lockless_read_key)" in f
        ),
        "filemap_and_shmem_release_do_not_hide_rcu_stub_call": (
            "<filemap_read_rcu>" not in (mainasm + sham)
            and sym["release"]["filemap_read_rcu"]["size"] == 8
        ),
        "G700_known_pinned_boot_hash": sha256(
            (P / "artifacts/G700-RCU-OFF-DEBUGFS-HOST-ONLY/BOOT-G700-RCU-OFF-DEBUGFS-HOST-ONLY-DO-NOT-FLASH.img").read_bytes()
        ).hexdigest() == "aec9655acc727ab01d0bde3711414f9606c58c5fdcafb6ae5c52a2d3346edc7f",
    }

checks=checks_for()
mutations = {
    "replace_filemap_zero_with_rcu_call": (
        "filemap","fast_read = 0;", "fast_read = filemap_read_rcu(iocb, iter, last_pos);"),
    "replace_shmem_zero_with_rcu_call": (
        "shmem","retval = 0;", "retval = filemap_read_rcu(iocb, to, last_pos);"),
    "drop_batch_try_get": (
        "filemap","if (!folio_try_get(folio))", "if (!folio_test_uptodate(folio))"),
    "drop_batch_reload": (
        "filemap","if (unlikely(folio != xas_reload(&xas)))", "if (unlikely(folio == NULL))"),
    "drop_reference_put": (
        "filemap","folio_put(folio);", "folio_mark_accessed(folio);"),
    "open_experimental_ON_switch": (
        "filemap","if (val)\n\t\treturn -EOPNOTSUPP;","if (val)\n\t\treturn 0;"),
}
detected={}
for name,(file,needle,replacement) in mutations.items():
    if needle not in SOURCES[file]:
        raise RuntimeError("Missing mutation fixture "+name)
    changed=dict(SOURCES)
    if name in ("replace_filemap_zero_with_rcu_call", "replace_shmem_zero_with_rcu_call"):
        marker = "#elif defined(CONFIG_PAGE_RCU_FREE)"
        start = SOURCES[file].index(marker)
        end = SOURCES[file].index("#endif", start)
        before_section = SOURCES[file][start:end]
        if needle not in before_section:
            raise RuntimeError("Release conditional changed unexpectedly: " + name)
        changed[file] = SOURCES[file][:start] + before_section.replace(needle,replacement,1) + SOURCES[file][end:]
    elif name == "drop_batch_try_get" or name == "drop_batch_reload" or name == "drop_reference_put":
        # Scope mutation to the real filemap_get_read_batch() implementation.
        begin=SOURCES[file].index("static void filemap_get_read_batch(")
        end=SOURCES[file].index("\nstatic int filemap_read_folio(",begin)
        prefix=SOURCES[file][:begin]
        block=SOURCES[file][begin:end]
        if needle not in block:
            raise RuntimeError("Fixture not in target batch function: "+name)
        changed[file]=prefix + block.replace(needle,replacement,1) + SOURCES[file][end:]
    else:
        changed[file]=SOURCES[file].replace(needle,replacement,1)
    try:
        detected[name]=not all(checks_for(changed).values())
    except (ValueError,RuntimeError):
        detected[name]=True
# Two binary corruption sentinels (without modifying linked images):
stubbad={key:{n: list(ins) for n,ins in value.items()} for key,value in dasm.items()}
stubbad["release"]["filemap_read_rcu"] = [
    *dasm["release"]["filemap_read_rcu"][:-1],
    {**dasm["release"]["filemap_read_rcu"][-1],"op":"nop"}
]
detected["reject_binary_stub_modified"] = not all(checks_for(SOURCES,stubbad).values())
callbad={key:{n:list(ins) for n,ins in value.items()} for key,value in dasm.items()}
callbad["release"]["filemap_read"] = [
    *dasm["release"]["filemap_read"],
    {"word":"eb000000","op":"bl","operand":"c02fdf08 <filemap_read_rcu>","pc":"00000000"}
]
detected["reject_binary_unref_call_inserted"] = not all(checks_for(SOURCES,callbad).values())

report={
    "stage":"G713",
    "scope":"Read-only linked ARM32 G700 release + G686 experiment: verify production filemap/shmem folio reference contract and accidental ref-free dispatch regressions",
    "gates":checks,
    "mutation_detection":detected,
    "linked_symbol_sizes":sym,
    "linked_function_instruction_counts":{profile:{f:len(x) for f,x in items.items()} for profile,items in dasm.items()},
    "factual_decision":"G700 filemap_read/shmem do not call ref-free stub; reference-backed filemap_get_read_batch uses tryget/reload/put; no G713 claim of filemap_read speedup over G650 (G694 compiled baseline had same instructions)",
    "known_limits":[
        "G700 full host-only ELF is not the running physical G650.",
        "Current G650 runtime static key cannot be read through unprivileged ADB; G692 fail-closed is STAGED only.",
        "Does not prove absence of all possible other ref-free callers; checks designated production read paths.",
        "Does not prove real ARMv7 memory ordering, mmap lockdep, S2 or S3.",
        "Does not grant unattended flash permission without verified current p9/p10 and recovery path."
    ],
    "release_candidate_rcu_noref_ON_allowed":False,
    "runtime_kernel_C_modified":False,
    "physical_boot_flashed":False
}
print(json.dumps(report,indent=2,ensure_ascii=False))
if not all(checks.values()) or not all(detected.values()):
    raise SystemExit(1)
