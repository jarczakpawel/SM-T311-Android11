/* Read-only physical KMS vblank progress. NEVER makes an atomic commit. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <xf86drm.h>

static int64_t diff_us(struct timespec a, struct timespec b) {
  return ((int64_t)a.tv_sec-(int64_t)b.tv_sec)*1000000LL +
    ((int64_t)a.tv_nsec-(int64_t)b.tv_nsec)/1000LL;
}
int main(void) {
  int fd=open("/dev/dri/card0",O_RDONLY|O_CLOEXEC);
  if(fd<0){perror("open");return 2;}
  unsigned last=0;
  for(int i=0;i<3;i++) {
    drmVBlank vblank={0};
    vblank.request.type=DRM_VBLANK_RELATIVE;
    vblank.request.sequence=1;
    struct timespec begin,end;
    if(clock_gettime(CLOCK_MONOTONIC,&begin)){close(fd);return 3;}
    if(drmWaitVBlank(fd,&vblank)){
      printf("KMS_VBLANK_WAIT_FAILED=%d ERRNO=%d\n",i,errno);
      close(fd);return 4;
    }
    if(clock_gettime(CLOCK_MONOTONIC,&end)){close(fd);return 5;}
    const unsigned seq=vblank.reply.sequence;
    printf("KMS_VBLANK_OBSERVATION=%d SEQ=%u WALL_US=%" PRId64 "\n",
      i,seq,diff_us(end,begin));
    if(i>0 && seq<=last){close(fd);return 6;}
    last=seq;
  }
  close(fd);
  puts("KMS_VBLANK_PROGRESS=PASS");
  return 0;
}
