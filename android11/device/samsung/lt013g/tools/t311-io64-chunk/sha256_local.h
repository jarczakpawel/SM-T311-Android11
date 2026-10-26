#ifndef G563_SHA256_LOCAL_H
#define G563_SHA256_LOCAL_H
#include <stddef.h>
#include <stdint.h>
typedef struct {
    uint32_t state[8];
    uint64_t total;
    unsigned char tail[64];
    size_t used;
} G563Sha256;
void g563_sha256_init(G563Sha256 *h);
void g563_sha256_update(G563Sha256 *h, const void *data, size_t count);
void g563_sha256_final(G563Sha256 *h, unsigned char output[32]);
#endif
