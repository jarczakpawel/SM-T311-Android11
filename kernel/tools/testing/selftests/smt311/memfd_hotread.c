// G648: exact SM-T311 refcounted shmem read-path smoke test; no RCU ON.
// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#define PAGE 4096
static void die(const char *s) { perror(s); exit(1); }
int main(void) {
#ifdef __NR_memfd_create
 int fd=(int)syscall(__NR_memfd_create,"g648-sm-t311-private-memfd",1U);
#else
 int fd=-1;errno=ENOSYS;
#endif
 if(fd<0)die("memfd_create");
 unsigned char out[PAGE],in[PAGE];
 for(size_t i=0;i<PAGE;i++)out[i]=(unsigned char)((i*29+17)&255);
 if(pwrite(fd,out,PAGE,0)!=PAGE)die("memfd initial write");
 for(int n=0;n<20000;n++) {
   off_t off=(n&1)?2048:0;
   if(pread(fd,in,16,off)!=16)die("memfd hot read");
   if(memcmp(in,out+off,16)) {fprintf(stderr,"G648_MISMATCH_READ at %d\n",n);return 2;}
 }
 if(ftruncate(fd,17))die("memfd truncate short");
 memset(in,0xAA,PAGE);
 if(pread(fd,in,PAGE,0)!=17)die("memfd short EOF read");
 if(memcmp(in,out,17)){fprintf(stderr,"G648_MISMATCH_TRUNCATE\n");return 3;}
 if(ftruncate(fd,PAGE))die("memfd extend");
 memset(in,0xAA,PAGE);
 if(pread(fd,in,PAGE,0)!=PAGE)die("memfd extended read");
 if(memcmp(in,out,17)){fprintf(stderr,"G648_MISMATCH_EXTEND_FIRST17\n");return 4;}
 for(size_t i=17;i<PAGE;i++)if(in[i]!=0){fprintf(stderr,"G648_MISMATCH_ZERO_AT=%zu\n",i);return 5;}
 printf("G648_ARMV7_SHMEM_PASS memfd create/pread 20000 alternating hot reads, truncate, extend and zero tail; RCU OFF\n");
 close(fd);
 return 0;
}
