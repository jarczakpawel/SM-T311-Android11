/* SM-T311 Exynos G2D hardware presence/version proof.
 * No GEM allocation, no command submission, no display/KMS calls.
 * DRM_EXYNOS_G2D_GET_VER is a read-only version ioctl (0x20).
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

struct g2d_ver { unsigned int major, minor; };
#define DRM_IOCTL_BASE 'd'
#define DRM_COMMAND_BASE 0x40
#define DRM_EXYNOS_G2D_GET_VER 0x20
#define DRM_IOCTL_EXYNOS_G2D_GET_VER \
    _IOWR(DRM_IOCTL_BASE, DRM_COMMAND_BASE + DRM_EXYNOS_G2D_GET_VER, struct g2d_ver)

int main(void) {
   const char *path = "/dev/dri/card0";
   struct g2d_ver ver = {0};
   int fd = open(path, O_RDWR | O_CLOEXEC);
   if (fd < 0) {
      fprintf(stderr, "G2D_OPEN_FAILED path=%s errno=%d error=%s\n",
              path, errno, strerror(errno));
      return 2;
   }
   int ret = ioctl(fd, DRM_IOCTL_EXYNOS_G2D_GET_VER, &ver);
   int err = errno;
   close(fd);
   if (ret < 0) {
      fprintf(stderr, "G2D_GET_VER_FAILED errno=%d error=%s\n",
              err, strerror(err));
      return 3;
   }
   printf("EXYNOS_G2D_GET_VER_HARDWARE_OK=%u.%u\n", ver.major, ver.minor);
   printf("EXYNOS_G2D_COMMAND_EXECUTED=NO (read-only ioctl)\n");
   return 0;
}
