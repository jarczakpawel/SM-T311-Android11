/* Isolated Exynos G2D DMA hardware test: one private 64x64 GEM BO.
 * NEVER uses FIMD display framebuffer, OpenGL, SurfaceFlinger or GPU VRAM.
 * One 16 KiB CPU mapped CMA allocation, one synchronous G2D fill.
 */
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "exynos_drmif.h"
#include "exynos_fimg2d.h"

int main(void) {
   const unsigned width = 64, height = 64;
   const size_t size = 64 * 64 * sizeof(uint32_t);
   const uint32_t expected = UINT32_C(0xff2873a5);
   struct exynos_device *dev = NULL;
   struct exynos_bo *bo = NULL;
   struct g2d_context *ctx = NULL;
   void *buf = NULL;
   int fd = -1, rc = 4;
   fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
   if (fd < 0) { perror("open card0"); return 2; }
   dev = exynos_device_create(fd);
   if (!dev) { fprintf(stderr, "EXYNOS_DEVICE_CREATE_FAIL\n"); goto end; }
   bo = exynos_bo_create(dev, size, EXYNOS_BO_CONTIG);
   if (!bo) { fprintf(stderr, "EXYNOS_GEM_CREATE_FAIL\n"); goto end; }
   buf = exynos_bo_map(bo);
   if (!buf) { fprintf(stderr, "EXYNOS_GEM_MAP_FAIL\n"); goto end; }
   memset(buf, 0, size);
   ctx = g2d_init(fd);
   if (!ctx) { fprintf(stderr, "G2D_CONTEXT_FAIL\n"); goto end; }
   struct g2d_image img = { 0 };
   img.width = width;
   img.height = height;
   img.stride = width * 4;
   img.color_mode = G2D_COLOR_FMT_ARGB8888 | G2D_ORDER_AXRGB;
   img.color = expected;
   img.buf_type = G2D_IMGBUF_GEM;
   img.bo[0] = bo->handle;
   if (g2d_solid_fill(ctx, &img, 0, 0, width, height) < 0) {
      fprintf(stderr, "G2D_QUEUE_FILL_FAIL\n"); goto end;
   }
   if (g2d_exec(ctx) < 0) { fprintf(stderr, "G2D_SYNC_EXEC_FAIL\n"); goto end; }
   const uint32_t *pixel = buf;
   unsigned mismatches = 0;
   for (unsigned i = 0; i < width * height; ++i)
      if (pixel[i] != expected) ++mismatches;
   printf("EXYNOS_G2D_PRIVATE_BO_FILL_PIXELS=%u\n", width * height);
   printf("EXYNOS_G2D_MISMATCHES=%u EXPECT=0x%08" PRIx32 " FIRST=0x%08" PRIx32 "\n",
          mismatches, expected, pixel[0]);
   rc = mismatches ? 5 : 0;
end:
   if (ctx) g2d_fini(ctx);
   if (bo) exynos_bo_destroy(bo);
   if (dev) exynos_device_destroy(dev);
   if (fd >= 0) close(fd);
   return rc;
}
