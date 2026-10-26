# Proprietary inputs — not included

| Dependency | Required for |
|---|---|
| Samsung/Broadcom firmware (84 local vendor files) | Wi-Fi, Bluetooth, camera/other hardware |
| Samsung XMM6262 RIL / CBD compatibility binaries | 3G, calls, SMS |
| BCM4752 legacy GNSS private inputs | GPS/AGPS |
| Mesa 26.2.5 ARM32 staged libraries | Mali-400 GPU / HWComposer |

Source device trees include expected checksums and staging rules. Obtain compatible firmware legally from your own device/firmware, subject to applicable licenses. **Do not blindly commit or distribute ignored binary files.**

The signed ROM and full-history Git bundles are staged outside Git. The upstream source manifest and verified patches reproduce tracked source, **not** closed-source vendor binary artifacts.
