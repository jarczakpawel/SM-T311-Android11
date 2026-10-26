#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <xf86drm.h>
#include <drm_fourcc.h>
#include "exynos_drm_6_12_112_uapi.h"

int main(void) {
    int fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd < 0) return perror("card0"), 2;
    struct drm_exynos_ioctl_ipp_get_res r = {0};
    if (drmIoctl(fd, DRM_IOCTL_EXYNOS_IPP_GET_RESOURCES, &r)) {
        printf("IPP_RES_ERROR=%d\n", errno);
        close(fd);
        return 3;
    }
    printf("IPP_COUNT=%u\n", r.count_ipps);
    if (!r.count_ipps || r.count_ipps > 16) { close(fd); return 4; }
    uint32_t ids[16] = {0};
    r.ipp_id_ptr = (uintptr_t)ids;
    if (drmIoctl(fd, DRM_IOCTL_EXYNOS_IPP_GET_RESOURCES, &r)) { close(fd); return 5; }
    int found = 0;
    for (unsigned i=0; i<r.count_ipps && i<16; i++) {
        struct drm_exynos_ioctl_ipp_get_caps c = {.ipp_id=ids[i]};
        if (drmIoctl(fd, DRM_IOCTL_EXYNOS_IPP_GET_CAPS, &c)) continue;
        printf("IPP_ID=%u CAP=0x%x FORMATS=%u\n", ids[i],c.capabilities,c.formats_count);
        if (!c.formats_count || c.formats_count > 32) continue;
        struct drm_exynos_ipp_format *f=calloc(c.formats_count,sizeof(*f));
        if (!f) break;
        c.formats_ptr = (uintptr_t)f;
        if (!drmIoctl(fd, DRM_IOCTL_EXYNOS_IPP_GET_CAPS, &c)) {
            for (unsigned j=0;j<c.formats_count;j++) {
                printf("FORMAT=%c%c%c%c type=0x%x modifier=%" PRIu64 "\n",
                    f[j].fourcc & 255,(f[j].fourcc>>8)&255,(f[j].fourcc>>16)&255,(f[j].fourcc>>24)&255,
                    f[j].type,(uint64_t)f[j].modifier);
                if (f[j].fourcc==DRM_FORMAT_XRGB8888 &&
                    (f[j].type & DRM_EXYNOS_IPP_FORMAT_SOURCE) &&
                    (c.capabilities & DRM_EXYNOS_IPP_CAP_ROTATE)) found=1;
            }
        }
        free(f);
    }
    printf("ROTATOR_XRGB8888_CAPABILITY=%s\n",found?"YES":"NO");
    close(fd);
    return found?0:6;
}
