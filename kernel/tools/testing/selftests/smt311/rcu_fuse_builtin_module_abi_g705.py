#!/usr/bin/env python3
import hashlib
import json
import subprocess
from pathlib import Path

kernel = Path(__file__).resolve().parents[4]
project = kernel.parent
builds = {
    label: project / "build" / name
    for label, name in (
        ("G686_EXP", "g686-reproducible-exp-on"),
        ("G700_OFF", "g700-debugfs-rcu-off"),
    )
}
source_makefile = (kernel / "fs/fuse/Makefile").read_text()
fuse_i = (kernel / "fs/fuse/fuse_i.h").read_text()
inode = (kernel / "fs/fuse/inode.c").read_text()
cuse = (kernel / "fs/fuse/cuse.c").read_text()
virtio = (kernel / "fs/fuse/virtio_fs.c").read_text()
g704 = json.loads((kernel / "Documentation/mm/sm-t311-rcu-fuse-release-g704.json").read_text())
configs = {label: (folder / ".config").read_text() for label, folder in builds.items()}
builtins = {label: (folder / "modules.builtin").read_text().splitlines() for label, folder in builds.items()}
module_files = {label: sorted(str(p.relative_to(folder)) for p in folder.rglob("*.ko")) for label, folder in builds.items()}
exports = {
    label: subprocess.run(
        ["arm-linux-gnueabihf-nm", "-a", str(folder / "vmlinux")],
        capture_output=True, text=True, check=True
    ).stdout
    for label, folder in builds.items()
}
references = subprocess.run(
    ["rg", "-l", r"fuse_conn_init\(|struct fuse_conn\b", "-g", "*.[ch]", "."],
    cwd=kernel, capture_output=True, text=True, check=True
).stdout.splitlines()
export_matches = {
    label: {
        symbol: [line for line in dump.splitlines() if line.endswith(" " + symbol)]
        for symbol in (
            "fuse_conn_init",
            "__ksymtab_fuse_conn_init",
            "__kstrtab_fuse_conn_init",
        )
    }
    for label, dump in exports.items()
}
fuse_builtin_path = "kernel/fs/fuse/fuse.ko"

def predicates(conf, records, extra_refs=None):
    refs = references if extra_refs is None else extra_refs
    return {
        "FUSE_FS_builtin_both": all("CONFIG_FUSE_FS=y" in c for c in conf.values()),
        "FUSE_PASSTHROUGH_builtin_both": all("CONFIG_FUSE_PASSTHROUGH=y" in c for c in conf.values()),
        "MODULES_enabled_but_MODVERSIONS_disabled": all(
            "CONFIG_MODULES=y" in c and "# CONFIG_MODVERSIONS is not set" in c
            for c in conf.values()
        ),
        "ZERO_configured_module_values_m": all(
            not any(line.startswith("CONFIG_") and line.endswith("=m") for line in c.splitlines())
            for c in conf.values()
        ),
        "FUSE_is_modules_builtin_entry_in_both": all(
            value.count(fuse_builtin_path) == 1 for value in records.values()
        ),
        "no_external_module_object_built": all(not rows for rows in module_files.values()),
        "CUSE_and_VIRTIO_FS_disabled_both": all(
            "# CONFIG_CUSE is not set" in c and "# CONFIG_VIRTIO_FS is not set" in c
            for c in conf.values()
        ),
        "FUSE_makefile_has_conditional_cuse_and_virtio": (
            "obj-$(CONFIG_FUSE_FS) += fuse.o" in source_makefile
            and "obj-$(CONFIG_CUSE) += cuse.o" in source_makefile
            and "obj-$(CONFIG_VIRTIO_FS) += virtiofs.o" in source_makefile
        ),
        "FUSE_makefile_links_in_tree_inode_and_iomode": (
            "inode.o control.o" in source_makefile and "fuse-y += iomode.o" in source_makefile
        ),
        "fuse_conn_init_all_in_tree_callers_located": (
            inode.count("fuse_conn_init(") == 2
            and cuse.count("fuse_conn_init(") == 1
            and virtio.count("fuse_conn_init(") == 1
            and fuse_i.count("fuse_conn_init(") == 1
        ),
        "fuse_conn_refs_only_under_fs_fuse": all(
            file.startswith("./fs/fuse/") for file in refs
        ) and len(refs) >= 15,
        "fuse_conn_init_GPL_export_in_source": "EXPORT_SYMBOL_GPL(fuse_conn_init);" in inode,
        "fuse_conn_init_real_GPL_symbol_table_present_both": all(
            all(len(matches) == 1 for matches in values.values())
            for values in export_matches.values()
        ),
        "G686_experiment_ON_G700_OFF": (
            "CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y" in conf["G686_EXP"]
            and "# CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT is not set" in conf["G700_OFF"]
        ),
        "G704_release_decision_preserved": (
            all(g704["checks"].values())
            and g704["decision"] == "KEEP_G700_DIAGNOSTIC_DISABLED"
            and not g704["external_modules_compatible_proven"]
        ),
    }

checks = predicates(configs, builtins)
negative = {
    "mutate_G700_FUSE_to_module": not all(predicates(
        {**configs, "G700_OFF": configs["G700_OFF"].replace(
            "CONFIG_FUSE_FS=y", "CONFIG_FUSE_FS=m", 1
        )}, builtins
    ).values()),
    "mutate_G700_builtins_without_FUSE": not all(predicates(
        configs, {**builtins, "G700_OFF": [
            item for item in builtins["G700_OFF"] if item != fuse_builtin_path
        ]}
    ).values()),
    "mutate_G686_turn_MODVERSIONS_on": not all(predicates(
        {**configs, "G686_EXP": configs["G686_EXP"].replace(
            "# CONFIG_MODVERSIONS is not set", "CONFIG_MODVERSIONS=y", 1
        )}, builtins
    ).values()),
    "mutate_out_of_tree_struct_consumer": not all(predicates(
        configs, builtins, references + ["./drivers/external/fuse_client.c"]
    ).values()),
}
report = {
    "stage": "G705",
    "scope": "Entire tracked kernel C/H reference inventory; real existing ARM32 ELF, .config and modules.builtin; no rebuild/ADB",
    "checks": checks,
    "negative_controls": negative,
    "references": sorted(references),
    "fuse_conn_init_source_caller_paths": {
        "built_in_main_FUSE": "fs/fuse/inode.c",
        "disabled_CUSE": "fs/fuse/cuse.c",
        "disabled_virtiofs": "fs/fuse/virtio_fs.c",
        "declaration": "fs/fuse/fuse_i.h",
    },
    "modules_builtin_fuse_marker": fuse_builtin_path,
    "modules_builtin_markers_are_not_loadable_ko_artifacts": True,
    "module_objects": module_files,
    "module_config_counts": {
        label: sum(line.startswith("CONFIG_") and line.endswith("=m") for line in content.splitlines())
        for label, content in configs.items()
    },
    "export_symbols": export_matches,
    "external_module_struct_layout_compatibility_proven": False,
    "kernel_binary_dependency_compatibility_proven": False,
    "physical_p9_p10_sha_verified": False,
    "G700_flash_authorized": False,
}
print(json.dumps(report, indent=2, ensure_ascii=False))
if not all(checks.values()) or not all(negative.values()):
    raise SystemExit(1)
