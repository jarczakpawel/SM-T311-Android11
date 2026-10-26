/* Exynos G2D bridge: sync_file verification + bounded GPU fence waits
 * BEFORE submitting a G2D DMA blit. Never used in per-frame production.
 */
#define _GNU_SOURCE 1
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <linux/sync_file.h>
#include <sync/sync.h>
#include "exynos_drm.h"
#include "exynos_fimg2d.h"
#include "g2d_bridge.h"

static uint64_t monotonic_ns(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts)) return 0;
  return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + ts.tv_nsec;
}
static int wait_sync_file_deadline(int fd, uint64_t deadline_ns) {
  if (fd == -1) return 0;
  if (fd < -1) return -EBADF;
  /* poll() on a random readable FD can spuriously report success; verify
   * the actual Linux sync_file ABI, including signaled error status.
   */
  struct sync_file_info info;
  memset(&info, 0, sizeof(info));
  if (ioctl(fd, SYNC_IOC_FILE_INFO, &info) != 0) return -EINVAL;
  if (info.status < 0) return -EIO;
  uint64_t now=monotonic_ns();
  if (!now || now >= deadline_ns) return -ETIMEDOUT;
  uint64_t remaining_ns=deadline_ns-now;
  int wait_ms=(int)(remaining_ns / UINT64_C(1000000));
  if (!wait_ms && remaining_ns) wait_ms=1;
  if (sync_wait(fd, wait_ms) != 0)
    return (errno == ETIME || errno == ETIMEDOUT) ? -ETIMEDOUT : -EIO;
  memset(&info, 0, sizeof(info));
  if (ioctl(fd, SYNC_IOC_FILE_INFO, &info) != 0 || info.status < 0)
    return -EIO;
  return 0;
}
int t311_g2d_copy_sync(struct g2d_context *ctx,
                       struct g2d_image *src, struct g2d_image *dst,
                       unsigned w, unsigned h, int acquire_fd,
                       int destination_release_fd, int total_wait_ms,
                       struct t311_g2d_timings *timings) {
  if (!ctx || !src || !dst || !w || !h || total_wait_ms < 1 ||
      total_wait_ms > 100) return -EINVAL;
  if (timings) memset(timings,0,sizeof(*timings));
  const uint64_t started=monotonic_ns();
  if (!started) return -EIO;
  const uint64_t deadline=started+(uint64_t)total_wait_ms*UINT64_C(1000000);
  int err=wait_sync_file_deadline(acquire_fd,deadline);
  if (err) return err; /* Submit NOTHING if producer not ready. */
  err=wait_sync_file_deadline(destination_release_fd,deadline);
  if (err) return err; /* Do not write to in-use scanout/consumer BO. */
  const uint64_t ready=monotonic_ns();
  if (ready>deadline) return -ETIMEDOUT;
  if (timings) timings->fence_wait_ns=ready-started;
  if (g2d_copy(ctx,src,dst,0,0,0,0,w,h)<0) return -EIO;
  if (g2d_exec(ctx)<0) return -EIO;
  if (timings) timings->g2d_exec_ns=monotonic_ns()-ready;
  return 0;
}
