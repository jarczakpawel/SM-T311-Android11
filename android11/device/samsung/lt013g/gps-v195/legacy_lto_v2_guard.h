#ifndef LT013G_LEGACY_LTO_V2_GUARD_H
#define LT013G_LEGACY_LTO_V2_GUARD_H

#include <stddef.h>
#include <stdint.h>

/*
 * G357. Validate the legacy Broadcom .dat envelope before any attempt
 * to feed the 2014 OEM gps-xtra extension.
 *
 * The only configured Android PSDS URL is the original Samsung SM-T311
 * gps.conf path /4day/glo/v2/latest/lto2.dat, upgraded to HTTPS.
 * This check is deliberately NOT proof the internal orbit format is valid.
 * The native injection remains OFF unless debug.lt013g.gps.v357.lto is set.
 */
static int lt013g_v357_lto_envelope_valid(const uint8_t *data, size_t length) {
    uint32_t payload_len;
    uint32_t format;
    if (!data || length < 1024 || length > (150u * 1024u)) return 0;
    if (data[0] != 0xff || data[1] != 0xca ||
        data[2] != 0xde || data[3] != 0x00) return 0;
    payload_len = (uint32_t)data[4] | ((uint32_t)data[5] << 8) |
                  ((uint32_t)data[6] << 16) | ((uint32_t)data[7] << 24);
    format = (uint32_t)data[8] | ((uint32_t)data[9] << 8) |
             ((uint32_t)data[10] << 16) | ((uint32_t)data[11] << 24);
    if (payload_len != length - 8 || format != 1) return 0;
    return 1;
}
#endif
