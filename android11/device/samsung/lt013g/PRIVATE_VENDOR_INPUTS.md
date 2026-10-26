# SM-T311 private device firmware inputs for G275 reproduction

The public Git checkout alone cannot reproduce the private proprietary payload of the verified SM-T311 G275 ROM. Do not add these binaries to public Git or distribute them without distribution rights. The values below are **checksums and instructions only**, not binary files or credentials. The user is responsible for any public release.

## Legacy GNSS (stock-only private inputs)

Place the previously verified original private archive at `device/samsung/lt013g/gps-v195/legacy-rootfs.tar`.

- Required original archive SHA-256: `559c4ac19e3be326788a429c890e3d72d08aff5ca324ac1d40ad5a3c679b83ae`.
- Before an initial private build, if the staged directories do not already exist, run `python3 device/samsung/lt013g/tools/stage_immutable_gnss_vendor.py` from the AOSP checkout.
- Its integrity checks split the original into **48 immutable private vendor files** and **7 mutable configuration files** packaged as `gps-v195/legacy-rootfs-mutable.tar`. They remain `.gitignore`-excluded.
- `gps-v195/immutable-code.mk` contains tracked build rules, but is **not** a replacement for the 48 private file contents.
- Do not overwrite existing validated staging simply to run the tool again. For exact output file hashes use the private G277 artifact manifest instead of publishing binary contents.

## XMM6262 modem private inputs

These files must be provided in `device/samsung/lt013g` with the exact hash shown, independently of the public source checkout:

| Relative private file | SHA-256 |
|---|---|
| `radio/prebuilt/t311-cbd` | `03a5758257050aa8ccf3b3e3e46b2f8b054acb4bdb968220dc0af8f8e8adaca4` |
| `radio/prebuilt/libsec-ril.so` | `432658fd34c966a8ed16b9548ca8d015383d26c6c732826a43363131d31e3a94` |
| `radio/prebuilt/libsecnativefeature.so` | `32f5dff375f45c6423b882e97c50103e5eb3e0b7928b848a43f8d70534b0f100` |
| `radio/prebuilt/libstlport.so` | `c0ca1cf2ca60ca80ac7898a036f41d71625d85c2f32b88251a79d7465d26803f` |
| `radio/prebuilt/libt311_cutils_compat.so` | `b080a16607afaac0438c5b1806964c19a191605cd1959b9904d31d86108923c7` |

They are consumed by `radio/Android.bp` and installed into the G275 vendor payload. No SIM PIN or personal radio state belongs in this source manifest.

## Mesa 26 experimental variant

`SMT311_USE_MESA26` is **not enabled** in the verified G275 target-files: G275 uses the standard `libGLES_mesa.so`, not the local optional `prebuilt/pjarczak/mesa26` experimental blobs. Do not include or publicly distribute those optional local binaries for a stock G275 rebuild.

## Reproducibility and deployment boundaries

The separate private G277 manifest verifies all **54 packaged private GNSS/RIL files byte-for-byte** against the G275 target-files archive. That is an input provenance check, **not** a claim that fresh clean checkout, boot, GNSS, all radio functions or SELinux Enforcing have been validated. Historical kernel prebuilt/module dumps and GNSS bridge temp archives are not requirements for the currently verified G275 package. Never flash or claim a new ROM is live without the independent installed-SYSTEM restore conditions and explicit approval.
