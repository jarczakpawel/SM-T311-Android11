# Source / build

| Item | Value |
|---|---|
| Android | LineageOS 18.1 (Android 11) |
| Managed Git repos | 863 |
| Patched upstream repos | 34, verified Git tree after `git apply` |
| Device trees | `lt013g`, `lt01-common` |
| Kernel | Linux 7.3.0-pjarczak source in `kernel/` |
| AOSP metadata | pinned G1441; release G1786 |

```sh
mkdir lineage-18.1-sm-t311 && cd lineage-18.1-sm-t311
repo init -u https://github.com/jarczakpawel/SM-T311-Android11.git -m android11/manifests/base.xml
repo sync -j4

# From a separate clone of this publication repository:
python3 ../SM-T311-Android11/android11/scripts/apply-patches.py "$PWD"
bash ../SM-T311-Android11/android11/scripts/install-device-trees.sh "$PWD"

# Add verified proprietary firmware and Mesa ARM32 inputs locally.
bash ../SM-T311-Android11/android11/scripts/build-g1786.sh "$PWD"
```

To restore the **exact original commit SHAs** rather than synthesized patch commits, extract `source-history` bundles from the Release assets after `repo sync` and run:

```sh
python3 ../SM-T311-Android11/android11/scripts/restore-original-git-history.py "$PWD" /path/to/source-history
```

Choose **either** patches **or** original-history bundles on a fresh base checkout, not both. Both must produce the same final tracked file trees. Proprietary blob inputs are required for a full image rebuild, and public Git alone cannot supply them.

See `android11/patches/index.json`, `android11/source-pins.json` and `android11/manifests/heads-original.xml` for exact source hashes.
