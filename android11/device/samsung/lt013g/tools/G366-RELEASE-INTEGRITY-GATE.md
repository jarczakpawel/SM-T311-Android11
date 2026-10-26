# SM-T311 G366 — Offline target-files release integrity gate

The release auditor compares a read-only SHA256 inventory captured from the running SM-T311 with a generated Android 11 target-files ZIP. It never invokes ADB, never modifies the tablet, and never reads SIM, PIN/PUK or EFS.

## Run

```sh
python3 device/samsung/lt013g/tools/test_audit_target_files_vs_live.py

python3 device/samsung/lt013g/tools/audit_target_files_vs_live.py \
  --zip /PATH/TO/NEW-target_files.zip \
  --live /home/debian/Projekty/samsung/artifacts/SM-T311-G366-RELEASE-GATE-20261016/live_sha256_inventory.txt \
  --boot-expected 9487424414439edf4dd00bf4169bcd684572cc4abe6690af9589632205666436 \
  --policy-analyzer out/host/linux-x86/bin/sepolicy-analyze \
  --report /home/debian/Projekty/samsung/artifacts/SM-T311-G366-RELEASE-GATE-20261016/new-release-audit.json
```

A complete release *must* have `status: PASS`: every inventoried vendor HAL/service matches the deployed baseline, the exact RIL and original patched Samsung RIL libraries match, BOOT prefix matches the saved observed hash, the compiled policy is matching and has **no permissive domain**, and the matching SystemUI APK/ODEX/VDEX DEX CRCs are verified. A `BLOCK` result is a diagnostic, not authorization to roll back, change firmware, or alter user data.

A new intended BOOT, modem, or GPS update necessarily differs from the previous live baseline. In that case the comparison must not be bypassed: document new intended output hashes, approve and validate the coherent update, then capture a new trusted live baseline *after* successful deployment. This strict baseline comparison alone does not prove that the file is safe to flash.

Current live G365 source:
- boot prefix SHA256 `9487424414439edf4dd00bf4169bcd684572cc4abe6690af9589632205666436`
- SystemUI APK `afc6f70e6be16a3f840dd47cfccb76ca9e649e2048a78baefcb595b15d433f95`
- SystemUI ODEX `c438feb53cc3bd0482f293d14fe7dee1b0659bd52391f70179cba24265a9b50f`
- SystemUI VDEX `b7a082b9f901af3d7459d4ff56c371e1a30a8530904faa6a354128ff8effe102`
- G362 Android RIL `e25997484993bbca427324470ce02bd86dc5dda0f8e507191f36aa4d99b5a945`

Known SELinux limitation: actual deployed global Enforcing policy (SHA `2131d3...`) still includes permissive domains `su`, `rild`, `lt013g_gps_legacy`, `backuptool`; final fully enforcing release must not be declared complete. Never remove `rild`/legacy GPS exceptions without real device test strategy.

## Tests and known old-package failure

Nine synthetic positive/negative tests PASS. G294 preserved target-files audit against actual live G365 BLOCKED: 38 file matches, 7 mismatches, 0 missing; plus BOOT mismatch and non-enforcing domains. This is the expected and correct result. Do not flash the old package.

Project human handoff: `/home/debian/Projekty/samsung/chat-history/handoffs/SM-T311-G366-RELEASE-GATE-20261016.md`.
