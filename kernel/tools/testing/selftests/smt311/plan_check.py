#!/usr/bin/env python3
"""G653 fail-closed SM-T311 plan gate (host only, zero device or Git mutations).

This checks source parity and whether any phase has been promoted before its
prerequisites. PASS means metadata/source structure is internally consistent,
not that lockless RCU has been formally verified or run safely on hardware.
"""
import json
import subprocess
from pathlib import Path

K = Path(__file__).resolve().parents[4]  # repo/tools/testing/selftests/smt311
P = K / "Documentation/mm/sm-t311-rcu-progress.json"
D = K / "Documentation/mm/sm-t311-rcu-plan.rst"
VALID = {"PASS", "ACTIVE", "BLOCKED", "PARTIAL", "FAIL"}
EXPECTED = [f"S{i}" for i in range(9)]


def fail_if(condition, message):
    if condition:
        raise AssertionError("FAIL: " + message)


def git(*args):
    return subprocess.check_output(["git", "-C", str(K), *args], text=True).strip()


def check():
    status = json.loads(P.read_text())
    docs = D.read_text()
    phases = status["phases"]
    labels = {x["id"]: x for x in phases}
    fail_if(list(labels) != EXPECTED, "stage S0-S8 missing, duplicated or reordered")
    fail_if(status["schema"] != "smt311-rcu-g653-v1", "unexpected status schema")
    fail_if(status["device"].split()[1] != "SM-T311", "wrong hardware board")
    fail_if(status["physical_boot_release"] != "G650", "physical BOOT changed without new verified status")
    fail_if(len(status["physical_boot_sha256_last_verified"]) != 64, "physical BOOT sha size")
    fail_if(status["physical_kernel_source_commit"] != "a2fca4558", "unreviewed runtime base")
    fail_if(status["github_push_by_owner_only"] is not True, "owner GitHub only")
    fail_if(len(status["mandatory_next_report"]) != 5, "mandatory per-turn reporting contract absent")
    fail_if("NEXT" not in docs or "PHYSICAL TABLET" not in docs, "reporting promise missing in kernel docs")
    fail_if(status["rcu_experiment_default_enabled"] is not False, "RCU default ON not validated")
    fail_if(status["rcu_ON_safety_proven"] is not False, "unsafe self-certification of RCU ON")
    fail_if(status["highmem_ref_free_safe"] is not False, "unsafe HighMem proof claim")
    for name, phase in labels.items():
        st = phase["status"]
        fail_if(st not in VALID, f"invalid status {name}={st}")
        fail_if(name not in docs, f"plan stage {name} missing from docs")
        for dep in phase["dependencies"]:
            fail_if(dep not in labels, f"unknown prerequisite {dep}")
            if st == "PASS":
                fail_if(labels[dep]["status"] != "PASS",
                        f"{name} promoted to PASS with {dep} not PASS")
        if st == "PASS":
            fail_if(not phase.get("evidence"), f"empty evidence {name}")
            fail_if(bool(phase.get("blocked_by")), f"{name} marked PASS while blocked")
    fail_if(labels["S0"]["status"] != "PASS", "unverified baseline")
    fail_if(labels["S1"]["status"] != "ACTIVE", "S1 falsely closed without formal audit")
    for st in ("S2", "S3", "S4", "S5", "S6"):
        fail_if(labels[st]["status"] != "BLOCKED", st + " improperly promoted")
    fail_if(labels["S7"]["status"] != "PARTIAL", "S7 archive audit/deployment signoff still partial")
    fail_if(labels["S8"]["status"] != "ACTIVE", "S8 rolling next-work handoff inactive")
    fail_if(not status.get("github_push_by_owner_only", False),
            "Only owner may publish; GitHub must not be performed by task")
    config = (K / "mm/filemap.c").read_text()
    flags = (K / "mm/page_alloc.c").read_text()
    fail_if("static DEFINE_STATIC_KEY_FALSE(lockless_read_key)" not in config,
            "reference-free fastpath no longer default OFF")
    fail_if("folio_zonenum(folio) != ZONE_NORMAL" not in config,
            "normal-zone HighMem safety gate removed")
    fail_if("smp_rmb();" not in config or "xas_reload(xas)" not in config,
            "source reader barrier / reload unexpectedly changed")
    fail_if("rcu_free_defer(folio);" not in flags, "RCU deferred free unexpectedly removed")
    fail_if("if (hot && lockless_read_enabled())" not in config,
            "G650 OFF-path fast branch missing")
    # A staged source patch must NEVER be mislabeled as physically deployed.
    # The real G650 BOOT/source SHA stays pinned until validated new flash.
    runtime_directories = ("mm", "fs", "kernel", "drivers", "arch", "include", "init")
    changed = git("diff", "--name-only", "a2fca4558", "HEAD", "--", *runtime_directories)
    changed_paths = changed.splitlines()
    # An optional *tracked CONFIG fragment* is not an executable kernel
    # source change. It is audited separately and must never silently
    # whitelist other files under arch/arm.
    experiment_fragment = "arch/arm/configs/sm_t311_rcu_experiment_g686.fragment"
    fail_if(changed_paths.count(experiment_fragment) != 1,
            "G686 tracked optional experimental fragment not accounted for")
    fragment = (K / experiment_fragment).read_text()
    fail_if("CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y" not in fragment or
            "CONFIG_DEBUG_FS=y" not in fragment or
            "CONFIG_PAGE_RCU_FREE=y" not in fragment,
            "G686 experimental Kconfig fragment accidentally disabled")
    safe_off_fragment = "arch/arm/configs/sm_t311_rcu_off_keep_debugfs_g700.fragment"
    fail_if(changed_paths.count(safe_off_fragment) != 1,
            "G700 optional diagnostics-preserving config fragment not accounted for")
    safe_fragment = (K / safe_off_fragment).read_text()
    fail_if("CONFIG_DEBUG_FS=y" not in safe_fragment or
            "# CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT is not set" not in safe_fragment or
            "# CONFIG_PAGE_RCU_FREE is not set" not in safe_fragment,
            "G700 must preserve DEBUG_FS and block ref-free RCU")
    staged = status.get("staged_runtime_code_changes_NOT_INSTALLED", [])
    allowed_config_overlays = {experiment_fragment,safe_off_fragment}
    changed_runtime_paths = [p for p in changed_paths if p not in allowed_config_overlays]
    fail_if(sorted(changed_runtime_paths) != sorted(staged),
            "Unrecorded executable source difference versus physical G650: " + changed[:150])
    fail_if(status["physical_kernel_source_commit"] != "a2fca4558",
            "Physical BOOT version was silently promoted without verified flash")
    if staged:
        fail_if(staged != ["mm/filemap.c", "mm/page_alloc.c", "mm/shmem.c", "mm/vma.c"],
                "G688 four staged runtime files whitelist unexpectedly changed")
        fail_if(config.count("!IS_ENABLED(CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT) &&") < 2,
                "G674 single/batch inode final tag preservation missing")
        fail_if(status.get("staged_runtime_code_change_git_commit") != "5a038ac99",
                "Staged G674 commit not pinned")
        fail_if(status.get("staged_runtime_code_change_git_commits") !=
                {"mm/filemap.c": "7d567ea29", "mm/page_alloc.c": "56c7fba9d",
                 "mm/shmem.c": "c439cc374", "mm/vma.c": "01e351a3d"},
                "Staged G674/G676/G680/G683 commits not pinned")
        shmem = (K / "mm/shmem.c").read_text()
        fail_if("#elif defined(CONFIG_PAGE_RCU_FREE)\n\tretval = filemap_read_rcu(iocb, to, last_pos);\n#else" not in shmem,
                "G688 release shmem zero stub call elimination lost")
        dispatch = config.split("size_t filemap_read_rcu(",1)[1].split("ssize_t filemap_read(",1)[0]
        fail_if(not (dispatch.index("i_sb->s_magic != EXT4_SUPER_MAGIC") <
                     dispatch.index("if (!user_backed_iter(iter))") <
                     dispatch.index("if (!lockless_read_enabled()")),
                "G689 kernel-iterator exclusion must precede ref-free reader runtime key")
        map_src = (K / "mm/mmap_lock.c").read_text()
        err_contract = map_src.split("static inline struct vm_area_struct *vma_start_read(",1)[1].split(
            "struct vm_area_struct *lock_vma_under_rcu(",1)[0]
        fail_if("err:\n\trcu_read_unlock();" not in err_contract or
                "rcu_read_unlock();\n\tmmgrab(other_mm);" not in err_contract,
                "G690 per-VMA helper must release RCU before any mmap fallback")
        fail_if("fallback:\n\tvma = lock_next_vma_under_mmap_lock(mm, vmi, from_addr);\n\trcu_read_lock();" not in map_src,
                "G690 RCU reacquire must follow sleepable mmap_read_lock fallback")
        fail_closed = config.split("static int lockless_read_set(",1)[1].split("DEFINE_DEBUGFS_ATTRIBUTE(",1)[0]
        fail_if(not (fail_closed.index("if (val > 1)") <
                     fail_closed.index("if (val)\n\t\treturn -EOPNOTSUPP;") <
                     fail_closed.index("static_branch_enable(&lockless_read_key);")),
                "G692 experimental RCU debugfs ON must return -EOPNOTSUPP before any key enable")
        fail_if("static DEFINE_STATIC_KEY_FALSE(lockless_read_key);" not in config,
                "G692 experimental no-ref key must start disabled")
        # G693: keep the existing reference-acquiring fallback rather
        # than pretending a raw pointer/flag comparison pins a folio.
        ref_source = config.split("void *filemap_get_entry(",1)[1].split(
            "/**\n * __filemap_get_folio_mpol",1)[0]
        fail_if(not (ref_source.index("rcu_read_lock();") <
                     ref_source.index("folio = xas_load(&xas);") <
                     ref_source.index("if (!folio_try_get(folio))") <
                     ref_source.index("if (unlikely(folio != xas_reload(&xas)))") <
                     ref_source.index("folio_put(folio);") <
                     ref_source.index("rcu_read_unlock();")),
                "G693 standard XArray reference acquisition and revalidation protocol missing")
        zone_snapshot = K / "Documentation/mm/sm-t311-rcu-zone-snapshot-g693.json"
        fail_if(not zone_snapshot.exists(), "G693 physical memory snapshot lost")
        # G694: hardware result depends on original G650 object assembly,
        # not on an unmeasured C-level expectation of saved cycles.
        g694 = K / "Documentation/mm/sm-t311-rcu-off-object-diff-g694.json"
        fail_if(not g694.exists(), "G694 independent ARM32 object evidence lost")
        diag = json.loads(g694.read_text())
        fail_if(diag.get("source_gates_passed") != 17 or
                not all(diag.get("source_and_machine_code_gates", {}).values()),
                "G694 machine-code truth gates not all PASS")
        # G695: distinguish physically deployed EXPERIMENT=y from
        # host RELEASE-OFF-only matches, do not claim a physical A/B.
        phys = K / "Documentation/mm/sm-t311-physical-profile-g695.json"
        report = K / "Documentation/mm/sm-t311-physical-binary-g695.json"
        fail_if(not phys.exists() or not report.exists(),
                "G695 measured physical profile/matched ARM32 evidence lost")
        profile = json.loads(phys.read_text())
        g695 = json.loads(report.read_text())
        fail_if(not (K / "tools/testing/selftests/smt311/build_g650_comparison_g695.sh").exists(),
                "G695 independent G650 profiles must be rebuildable from Git")
        fail_if(profile.get("physical_config_to_host_experiment_diff_count")!=3 or
                "CONFIG_PAGE_RCU_FREE=y" not in profile.get("enabled",[]) or
                g695.get("positive_guard_count")!=19 or
                not all(g695.get("guards",{}).values()),
                "G695 physical configuration provenance / machine truth broken")
        # G696: an offline complete 8MiB boot must NEVER be mistaken
        # for a safely deployable/physical qualified kernel image.
        m696 = K / "Documentation/mm/sm-t311-rcu-offline-boot-g696.json"
        fail_if(not m696.exists(), "G696 offline complete BOOT gate missing")
        x696 = json.loads(m696.read_text())
        fail_if(x696.get("positive_gates")!=24 or
                x696.get("flash_authorized") is not False or
                x696.get("physical_live_boot_sha_confirmed") is not False or
                not all(x696.get("source_and_actual_8MiB_binary_guards",{}).values()),
                "G696 host BOOT safety preflight is not complete")
        # G697 preserve actual cross-profile driver/module ABI evidence,
        # never mistake symbol-name overlap for live hardware acceptance.
        r697 = K / "Documentation/mm/sm-t311-builtin-abi-g697.json"
        fail_if(not r697.exists(), "G697 physical driver symbol comparison missing")
        obj697 = json.loads(r697.read_text())
        fail_if(obj697.get("positive_count") != 17 or
                obj697.get("physical_flash_allowed") is not False or
                obj697.get("dynamic_RIL_and_GNSS_compatibility_proven") is not False or
                not all(obj697.get("positive_guards", {}).values()),
                "G697 offline driver ABI gate invalid")
        # G698: cross-repo Android HAL/init/SELinux ABI remains a
        # static G650 snapshot, NOT physical RCU OFF certification.
        r698 = K / "Documentation/mm/sm-t311-userspace-contract-g698.json"
        fail_if(not r698.exists(), "G698 userspace/kernel source contract missing")
        j698 = json.loads(r698.read_text())
        fail_if(j698.get("positive_count") != 20 or
                j698.get("flash_authorized") is not False or
                j698.get("physical_RCU_OFF_tested") is not False or
                not all(j698.get("positive_source_device_contracts", {}).values()),
                "G698 HAL/RIL/GPS security contract failed")
        # G699: golden HOST recovery is not a fresh live p10 hash.
        guard699 = K / "Documentation/mm/sm-t311-boot-recovery-g699.json"
        fail_if(not guard699.exists(), "G699 image identity report missing")
        g699 = json.loads(guard699.read_text())
        fail_if(g699.get("positive") != 18 or
                g699.get("flash_authorized") is not False or
                g699.get("current_live_BOOT_SHA_read") is not False or
                g699.get("current_live_RECOVERY_SHA_read") is not False or
                g699.get("manual_TWRP_test_executed") is not False or
                not all(g699.get("checks",{}).values()),
                "G699 raw BOOT/RECOVERY safety preflight incomplete")
        # G700 retain DEBUG_FS diagnostics, not no-ref RCU; do
        # not mistake a complete host image for a physical boot.
        target700 = K / "Documentation/mm/sm-t311-rcu-debugfs-off-g700.json"
        fail_if(not target700.exists(), "G700 release profile report missing")
        g700 = json.loads(target700.read_text())
        fail_if(g700.get("positive_count") != 21 or
                g700.get("DEBUGFS_preserved") is not True or
                g700.get("PAGE_RCU_FREE_enabled") is not False or
                g700.get("SM_T311_RCU_HOT4K_EXPERIMENT_enabled") is not False or
                g700.get("flash_authorized") is not False or
                g700.get("physical_BOOT_hash_reverified") is not False or
                g700.get("physical_recovery_hash_reverified") is not False or
                not all(g700.get("result",{}).values()),
                "G700 debugfs-only RCU OFF safety gate failed")
        # G701: compiled debugfs is NOT a runtime-mounted debugfs.
        # Do not count a passive G650 observation as a G700 boot.
        g701path = K / "Documentation/mm/sm-t311-rcu-debugfs-runtime-g701.json"
        fail_if(not g701path.exists(), "G701 DEBUG_FS runtime evidence missing")
        g701 = json.loads(g701path.read_text())
        fail_if(g701.get("positive_count") != 19 or
                g701.get("debugfs_mounted_on_G650") is not False or
                g701.get("G700_physically_booted") is not False or
                g701.get("flash_authorized") is not False or
                g701.get("real_partition_identity_confirmed") is not False or
                not all(g701.get("positive_gates",{}).values()),
                "G701 mounted DEBUG_FS cannot be inferred from build config")
        read_recheck = config.split("static bool filemap_read_rcu_batch(",1)[1].split("size_t filemap_read_rcu(",1)[0]
        fail_if(not (read_recheck.index("rcu_read_lock();") <
                     read_recheck.index("if (unlikely(!lockless_read_enabled()))") <
                     read_recheck.index("mapping_writably_mapped(mapping)") <
                     read_recheck.index("filemap_read_rcu_lookup(")),
                "G685 key check must stay INSIDE reader RCU before speculative lookup")
        fail_if("if (unlikely(!lockless_read_enabled())) {\n\t\trcu_read_unlock();\n\t\treturn false;" not in read_recheck,
                "G685 key rejection must unwind RCU and fall back")
        mmap_source = (K / "mm/vma.c").read_text()
        read_batch = config.split("static bool filemap_read_rcu_batch(",1)[1].split("size_t filemap_read_rcu(",1)[0]
        mmap_func = mmap_source.split("unsigned long mmap_region(",1)[1].split("do_brk_flags(",1)[0]
        fail_if(not (read_batch.index("rcu_read_lock();") <
                     read_batch.index("mapping_writably_mapped(mapping)")),
                "G680 reader writable mmap check outside RCU")
        fail_if(not (mmap_func.index("mapping_map_writable(file->f_mapping)") <
                     mmap_func.index("synchronize_rcu();") <
                     mmap_func.index("__mmap_region(")),
                "G680 new writable VMA published without RCU GP")
        fail_if("file->f_mapping->host->i_sb->s_magic == EXT4_SUPER_MAGIC" not in mmap_func or
                "mapping_rcu_read(file->f_mapping)" in mmap_func.split("/*",1)[0],
                "G683 mmap GP must be ext4-scoped and independent of stale AS_RCU_READ")
        fail_if("file->f_mapping->host->i_sb->s_magic == EXT4_SUPER_MAGIC" not in mmap_func,
                "G683 ext4 scope missing")
        fail_if("flush_rcu_work(&r->rwork);" not in flags,
                "G676 full RCU callback/work completion check absent")
        fail_if("flush_work(&r->rwork.work);" in flags,
                "Generic flush_work reintroduced for RCU work")
        fail_if(status["rcu_ON_safety_proven"] is not False, "RCU ON incorrectly promoted")
    return {
      "board": status["device"],
      "kernel_git_HEAD":git("rev-parse", "--short=9", "HEAD"),
      "physically_installed_BOOT_source":"a2fca4558",
      "physical_boot_sha256_last_verified":status["physical_boot_sha256_last_verified"],
      "RCU_default_ON":False,
      "HighMem_reference_free_proven":False,
      "stages":{k: v["status"] for k,v in labels.items()},
      "runtime_code_unchanged_since_G650":not bool(staged),
      "staged_runtime_diff_NOT_INSTALLED":staged,
      "warning":"Source/metadata gate ONLY; does not prove LKMM, ON safety or physical BOOT SHA at current time."
    }


if __name__ == "__main__":
    print(json.dumps(check(), ensure_ascii=False, indent=2))
