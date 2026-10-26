/* Isolated, OFFLINE G2D producer/consumer sync barrier. Not a HAL. */
#pragma once
#include <stdint.h>
struct g2d_context;
struct g2d_image;
struct t311_g2d_timings {
  uint64_t fence_wait_ns;
  uint64_t g2d_exec_ns;
};
/* Does NOT take ownership of caller fences. fd=-1 means no outstanding fence.
 * BOTH source producer and destination consumer release must finish before
 * G2D may read/write the BOs; do not touch KMS-active scanout BOs.
 */
int t311_g2d_copy_sync(struct g2d_context *ctx,
                       struct g2d_image *src, struct g2d_image *dst,
                       unsigned w, unsigned h,
                       int producer_acquire_fd, int destination_release_fd,
                       int total_wait_ms, struct t311_g2d_timings *timings);
