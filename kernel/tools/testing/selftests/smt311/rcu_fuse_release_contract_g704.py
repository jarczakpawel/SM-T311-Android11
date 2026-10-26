#!/usr/bin/env python3
import hashlib
import json
import subprocess
from pathlib import Path

kernel = Path(__file__).resolve().parents[4]
project = kernel.parent
builds = {
    name: project / "build" / directory
    for name, directory in (
        ("G686", "g686-reproducible-exp-on"),
        ("G700", "g700-debugfs-rcu-off"),
    )
}
configs = {name: (path / ".config").read_text() for name, path in builds.items()}
symbols = {
    name: subprocess.run(
        ["arm-linux-gnueabihf-nm", "-a", str(path / "vmlinux")],
        text=True, capture_output=True, check=True
    ).stdout
    for name, path in builds.items()
}
fuse = {
    name: (kernel / "fs/fuse" / name).read_text()
    for name in ("fuse_i.h", "inode.c", "iomode.c", "control.c")
}
source_refs = {
    name: [line.strip() for line in content.splitlines() if "passthrough_successful_opens" in line]
    for name, content in fuse.items()
}
base = json.loads((kernel / "Documentation/mm/sm-t311-rcu-fuse-binary-g703.json").read_text())
boot = project / "artifacts/G700-RCU-OFF-DEBUGFS-HOST-ONLY/BOOT-G700-RCU-OFF-DEBUGFS-HOST-ONLY-DO-NOT-FLASH.img"
sha = hashlib.sha256(boot.read_bytes()).hexdigest()
guard = "#ifdef CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT"
checks = {
    "G703_previous_binary_audit_complete": all(base["checks"].values()),
    "FUSE_diagnostic_exactly_six_experiment_guards": sum(x.count(guard) for x in fuse.values()) == 6,
    "FUSE_counter_all_references_known_in_four_files": sum(map(len, source_refs.values())) == 4,
    "FUSE_counter_init_increment_and_read_covered": all(
        piece in fuse[file]
        for file, piece in (
            ("fuse_i.h", "atomic64_t passthrough_successful_opens;"),
            ("inode.c", "atomic64_set(&fc->passthrough_successful_opens, 0);"),
            ("iomode.c", "atomic64_inc(&fc->passthrough_successful_opens);"),
            ("control.c", "atomic64_read(&fc->passthrough_successful_opens);"),
        )
    ),
    "G700_FUSE_optional_diagnostic_absent_by_config": (
        "# CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT is not set" in configs["G700"]
        and "CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y" in configs["G686"]
    ),
    "both_FUSE_passthrough_DEBUGFS_builtin": all(
        "CONFIG_FUSE_FS=y" in cfg and "CONFIG_FUSE_PASSTHROUGH=y" in cfg
        and "CONFIG_DEBUG_FS=y" in cfg for cfg in configs.values()
    ),
    "both_kernels_accept_modules_without_modversions": all(
        "CONFIG_MODULES=y" in cfg and "# CONFIG_MODVERSIONS is not set" in cfg
        for cfg in configs.values()
    ),
    "fuse_conn_init_kernel_export_present_both": all(
        " __ksymtab_fuse_conn_init" in nm and " __kstrtab_fuse_conn_init" in nm
        and " T fuse_conn_init" in nm for nm in symbols.values()
    ),
    "no_modules_built_in_either_host_profile": all(
        not list(path.rglob("*.ko")) for path in builds.values()
    ),
    "FUSE_module_features_cuse_and_virtiofs_disabled": all(
        "# CONFIG_CUSE is not set" in cfg and "# CONFIG_VIRTIO_FS is not set" in cfg
        for cfg in configs.values()
    ),
    "FUSE_sources_unchanged_from_G650": not subprocess.run(
        ["git", "diff", "--name-only", "a2fca4558", "HEAD", "--", "fs/fuse"],
        cwd=kernel, text=True, capture_output=True, check=True
    ).stdout.strip(),
    "G700_existing_full_BOOT_unchanged": (
        sha == "aec9655acc727ab01d0bde3711414f9606c58c5fdcafb6ae5c52a2d3346edc7f"
    ),
}
report = {
    "stage": "G704",
    "checks": checks,
    "diagnostic": "passthrough_successful_opens",
    "decision": "KEEP_G700_DIAGNOSTIC_DISABLED",
    "rationale": "Optional per-successful-passthrough-open atomic64 counter is not required by FUSE; avoid added ARMv7 work, struct-layout differences, and Kconfig complexity in the RCU-off release",
    "kernel_export_fuse_conn_init": True,
    "external_modules_compatible_proven": False,
    "host_profile_module_files": {name: 0 for name in builds},
    "G700_boot_sha256": sha,
    "G700_boot_installed": False,
    "physical_boot_and_recovery_current_sha_verified": False,
    "flash_authorized": False,
}
print(json.dumps(report, ensure_ascii=False, indent=2))
if not all(checks.values()):
    raise SystemExit(1)
