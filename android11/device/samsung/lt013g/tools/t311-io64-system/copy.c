#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <linux/fs.h>

#define T311_BUFSIZE (1024u*1024u)
#define EXPECTED_SIZE INT64_C(2235564032)
#define CHUNK_SIZE (INT64_C(512)*1024*1024)
#define SYSTEM_DEVICE "/dev/block/mmcblk0p20"
#define NEW_IMAGE "/data/local/tmp/SM-T311-SYSTEM-MESA26-20261010-RAW.img"
#define OLD_IMAGE "/data/local/tmp/SM-T311-SYSTEM-LIVE-BEFORE-MESA26-20261010.img"

static void die(const char *what) {
    fprintf(stderr,"T311_IO64_FAIL %s: %s\n",what,strerror(errno));
    exit(2);
}
static void wr64(int fd,const void *p,size_t n,int64_t off) {
    size_t done=0;
    while(done<n) {
      ssize_t x=pwrite64(fd,(const char*)p+done,n-done,(off64_t)(off+done));
      if(x<=0)die("pwrite64");
      done+=(size_t)x;
    }
}
static void rd64(int fd,void *p,size_t n,int64_t off) {
    size_t done=0;
    while(done<n) {
      ssize_t x=pread64(fd,(char*)p+done,n-done,(off64_t)(off+done));
      if(x<=0)die("pread64");
      done+=(size_t)x;
    }
}
static void check_system_device(int fd) {
    uint64_t size=0;
    if(ioctl(fd,BLKGETSIZE64,&size)<0)die("BLKGETSIZE64");
    if(size!=(uint64_t)EXPECTED_SIZE) {
      fprintf(stderr,"T311_IO64_FAIL WRONG_SYSTEM_DEVICE_SIZE=%llu\n",(unsigned long long)size);
      exit(5);
    }
}
static void require_twrp(void) {
    struct stat st;
    if(stat("/sbin/recovery",&st)!=0 || !S_ISREG(st.st_mode)) {
        fprintf(stderr,"T311_IO64_FAIL RECOVERY_ONLY\n");
        exit(7);
    }
}
static void check_src_size(int fd) {
    struct stat64 st;
    if(fstat64(fd,&st)<0)die("fstat64 input");
    if(st.st_size!=EXPECTED_SIZE) {
      fprintf(stderr,"T311_IO64_FAIL SRC_BYTES=%lld EXPECTED=%lld\n",
       (long long)st.st_size,(long long)EXPECTED_SIZE);
      exit(6);
    }
}
static void probe(void) {
    const char *filename="/data/local/tmp/T311-IO64-SPARSE-PROBE.tmp";
    int fd=open(filename,O_CREAT|O_EXCL|O_RDWR|O_CLOEXEC,0600);
    if(fd<0)die("create probe");
    const char a[8]={'T','3','1','1','I','O','6','4'};
    char b[8]={0};
    const int64_t hi=INT64_C(2147483648)+INT64_C(4096);
    wr64(fd,a,sizeof(a),hi);
    rd64(fd,b,sizeof(b),hi);
    if(memcmp(a,b,sizeof(a))) {
      fprintf(stderr,"T311_IO64_PROBE=FAIL\n");
      exit(8);
    }
    struct stat64 st;
    if(fstat64(fd,&st))die("fstat64 probe");
    if(st.st_size!=hi+(int64_t)sizeof(a)) {
      fprintf(stderr,"T311_IO64_PROBE_SIZE=FAIL\n");
      exit(9);
    }
    if(close(fd)<0)die("close probe");
    if(unlink(filename)<0)die("unlink probe");
    printf("T311_IO64_PROBE=PASS high_offset=%lld size=%lld\n",
      (long long)hi,(long long)st.st_size);
}
static void transfer(const char *source,int part,int only_verify) {
    if(part<0||part>4)exit(12);
    require_twrp();
    int in=open(source,O_RDONLY|O_CLOEXEC|O_LARGEFILE);
    if(in<0)die("open input");
    check_src_size(in);
    int out=open(SYSTEM_DEVICE,(only_verify?O_RDONLY:O_RDWR)|O_CLOEXEC|O_LARGEFILE);
    if(out<0)die("open block device");
    check_system_device(out);
    int64_t offset=(int64_t)part*CHUNK_SIZE;
    int64_t length=EXPECTED_SIZE-offset;
    if(length>CHUNK_SIZE)length=CHUNK_SIZE;
    char *src=(char*)malloc(T311_BUFSIZE);
    char *dst=(char*)malloc(T311_BUFSIZE);
    if(!src||!dst)die("malloc");
    printf("T311_IO64_START part=%d offset=%lld bytes=%lld only_verify=%d\n",
      part,(long long)offset,(long long)length,only_verify);
    fflush(stdout);
    for(int64_t n=0;n<length;n+=T311_BUFSIZE) {
      size_t amt=(size_t)((length-n)>T311_BUFSIZE?T311_BUFSIZE:(length-n));
      rd64(in,src,amt,offset+n);
      if(!only_verify)wr64(out,src,amt,offset+n);
    }
    if(!only_verify && fsync(out)<0)die("fsync block");
    for(int64_t n=0;n<length;n+=T311_BUFSIZE) {
      size_t amt=(size_t)((length-n)>T311_BUFSIZE?T311_BUFSIZE:(length-n));
      rd64(in,src,amt,offset+n);
      rd64(out,dst,amt,offset+n);
      if(memcmp(src,dst,amt)) {
        fprintf(stderr,"T311_IO64_VERIFY=FAIL part=%d offset=%lld\n",
          part,(long long)(offset+n));
        exit(14);
      }
    }
    free(src);free(dst);close(out);close(in);
    printf("T311_IO64_PART_PASS part=%d bytes=%lld\n",part,(long long)length);
}
int main(int argc,char **argv) {
   require_twrp();
   if(argc==2 && !strcmp(argv[1],"probe")) {
      probe();return 0;
   }
   if(argc!=4) {
       fprintf(stderr,"usage: t311_io64_system probe | mesa|restore copy|verify 0..4\n");
       return 11;
   }
   const char *src=NULL;
   if(!strcmp(argv[1],"mesa"))src=NEW_IMAGE;
   else if(!strcmp(argv[1],"restore"))src=OLD_IMAGE;
   else return 11;
   int only_verify=!strcmp(argv[2],"verify");
   if(!only_verify && strcmp(argv[2],"copy"))return 11;
   if(strlen(argv[3])!=1 || argv[3][0]<'0' || argv[3][0]>'4')return 11;
   transfer(src,argv[3][0]-'0',only_verify);
   return 0;
}
