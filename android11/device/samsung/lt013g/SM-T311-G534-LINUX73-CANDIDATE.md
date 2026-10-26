# G534 — Linux 7.3.0-pjarczak / Android 11 offline integration

**NOT INSTALLED OR HARDWARE QUALIFIED.** The tablet remains on physically validated G513 kernel BOOT p9 `4eed6731b7a6bed09f0337cdf6864c1d982d4cf66bc12d510e1dfbe2741e35eb`; TWRP p10 `f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f`. Known failed G514/G519 modem variants excluded.

G534 Linux7.3.0-pjarczak **snapshot** kernel source is `/home/debian/Projekty/samsung/linux-7.3-port/upstream-v7.3`, at build timestamp Oct19 2026; source has subsequently evolved, so this BOOT is now deliberately a **frozen offline candidate**, never silently promoted.

- `prebuilt/kernel-7.3.0-pjarczak-G534-CANDIDATE.bin`, size 6367257, SHA256 `ec213933f189d4df552a5a8f15ec70a4ecd03854a968eec88ab2aee36bb3161d`.
- `prebuilt/BOOT-7.3.0-pjarczak-G534-OFFLINE-CANDIDATE-8MiB.img`, size 8388608, SHA256 `34b55bc7d7c7265c272bf0366d39878bb40c2cda480fe792956c0a896c60ec17`.
- Base image is exact G513 8MiB BOOT SHA `4eed6731b7a6bed09f0337cdf6864c1d982d4cf66bc12d510e1dfbe2741e35eb`. Ramdisk SHA `681d59437776b09ede48dc57e16b34697d08b7161f820673b8c84df3943bd8fd`, size 786423 bytes. Repack tool verified byte-identical ramdisk, second stage, boot header except kernel_size and legacy ID. 8MiB geometry retained.
- Original host generation proof: `/home/debian/Projekty/samsung/linux-7.3-port/artifacts/G534-7.3-pjarczak-OFFLINE-CANDIDATE/repack-proof.json`.
- Product make selects verified Mesa **26.2.4** runtime libraries in `device/samsung/lt01-common/prebuilt/pjarczak/mesa26/`, and preserves the **golden** audio HAL and bounded G524 audio-RIL bridge. Both device repositories already pin their checksum manifests in Git.
- `tools/check_release_components.py` is fail-closed: verifies both BOOTABLE_IMAGES/ and IMAGES/ copies of BOOT and recovery, five Mesa26 binaries, golden audio and bridge SHA; rejects old combined Mesa20 libGLES_mesa.
- Existing target-files from G509 fails because it contains old G485 BOOT and Mesa20. A candidate Android11 rebuild is needed and is **not equal to a usable OTA**. G534 has not passed the functional Android11/USB/MTP/modem/HANGUP/DSIM/GNSS/audio/camera/SELinux checks.

**No flash G534** until Linux7.3 port's `MIGRATION_LEDGER.tsv` is resolved for essential drivers, runtime boot test is planned with guarded G513 fallback and test owner is present. Rebuild and repin BOOT from final kernel before real use. Test phone hangup twice because previous G514/G519 variants crashed AudioPolicy/Samsung USB, even if first call disconnected quickly.
