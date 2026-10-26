#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

static int xioctl(int f, unsigned long op, void *arg) {
    int rc;
    do { rc=ioctl(f,op,arg); } while(rc<0 && errno==EINTR);
    return rc;
}
static int failure(const char *where) {
    fprintf(stderr,"V4L2_CPU_ERROR %s errno=%d %s\n", where,errno,strerror(errno));
    return 3;
}
int main(int argc, char **argv) {
    setvbuf(stdout,NULL,_IONBF,0);
    int allocs=argc>1?atoi(argv[1]):8;
    int passes=argc>2?atoi(argv[2]):16;
    if(allocs<1||allocs>24||passes<1||passes>80) return 2;
    const unsigned w=256,h=256;
    int f=open("/dev/video2",O_RDWR|O_NONBLOCK|O_CLOEXEC);
    if(f<0)return failure("OPEN_FIMC");
    struct v4l2_format out={.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE};
    out.fmt.pix_mp.width=w;
    out.fmt.pix_mp.height=h;
    out.fmt.pix_mp.pixelformat=V4L2_PIX_FMT_NV12M;
    out.fmt.pix_mp.num_planes=2;
    if(xioctl(f,VIDIOC_S_FMT,&out)<0){close(f);return failure("SET_OUT_FMT");}
    struct v4l2_format cap={.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE};
    cap.fmt.pix_mp.width=w;
    cap.fmt.pix_mp.height=h;
    cap.fmt.pix_mp.pixelformat=V4L2_PIX_FMT_NV12;
    cap.fmt.pix_mp.num_planes=1;
    if(xioctl(f,VIDIOC_S_FMT,&cap)<0){close(f);return failure("SET_CAPTURE_FMT");}
    if(cap.fmt.pix_mp.num_planes!=1||cap.fmt.pix_mp.pixelformat!=V4L2_PIX_FMT_NV12){close(f);return failure("CAPTURE_FORMAT_UNEXPECTED");}
    size_t n=cap.fmt.pix_mp.plane_fmt[0].sizeimage;
    printf("V4L2_CPU_FMT width=%u height=%u n=%zu stride=%u\n",cap.fmt.pix_mp.width,cap.fmt.pix_mp.height,n,cap.fmt.pix_mp.plane_fmt[0].bytesperline);
    if(n<65536||n>262144){close(f);return failure("BUFFER_SIZE_UNEXPECTED");}
    uint8_t *src=malloc(n),*ordinary=malloc(n);
    if(!src||!ordinary){close(f);free(src);free(ordinary);return failure("malloc");}
    unsigned long long total=0;
    unsigned failed=0, idle_changes=0,ram_failed=0;
    for(int a=0;a<allocs;++a){
        struct v4l2_requestbuffers req={.count=1,.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,.memory=V4L2_MEMORY_MMAP};
        if(xioctl(f,VIDIOC_REQBUFS,&req)<0 || !req.count){failure("CAP_REQBUFS");break;}
        struct v4l2_buffer b={.type=req.type,.memory=req.memory,.length=VIDEO_MAX_PLANES};
        struct v4l2_plane plane[VIDEO_MAX_PLANES]={0};
        b.m.planes=plane;
        if(xioctl(f,VIDIOC_QUERYBUF,&b)<0 || b.length!=1){failure("CAP_QUERYBUF");break;}
        size_t length=plane[0].length;
        printf("V4L2_MMAP_ALLOC=%d size=%zu\n",a,length);
        if(length<n){fprintf(stderr,"UNDERSIZED_BUFFER\n");break;}
        uint8_t *dst=mmap(NULL,length,PROT_READ|PROT_WRITE,MAP_SHARED,f,plane[0].m.mem_offset);
        if(dst==MAP_FAILED){failure("CAP_MMAP");break;}
        for(int k=0;k<passes;++k){
            uint32_t x=0x2184bcdeU ^ (uint32_t)(a*1031+k*161);
            for(size_t j=0;j<n;++j){x^=x<<13;x^=x>>17;x^=x<<5;src[j]=(uint8_t)x;}
            memcpy(ordinary,src,n);
            if(memcmp(ordinary,src,n))ram_failed++;
            memcpy(dst,src,n);
            unsigned long long wrong=0;
            size_t first=0;
            for(size_t j=0;j<n;++j)
                if(dst[j]!=src[j]){if(!wrong)first=j;++wrong;}
            if(wrong){
                ++failed;total+=wrong;
                printf("V4L2_CPU_MISMATCH allocation=%d pass=%d wrong=%llu first=%zu got=%u want=%u\n",a,k,wrong,first,dst[first],src[first]);
            }
            if(getenv("T311_V4L2_IDLE")){
                usleep(2000);
                unsigned long long now=0;
                for(size_t j=0;j<n;++j)if(dst[j]!=src[j])++now;
                if(now!=wrong){
                    idle_changes++;
                    printf("V4L2_IDLE_CHANGE allocation=%d pass=%d before=%llu after=%llu\n",a,k,wrong,now);
                }
            }
        }
        munmap(dst,length);
        req.count=0;
        if(xioctl(f,VIDIOC_REQBUFS,&req)<0){failure("CAP_RELEASE_BUFS");break;}
    }
    printf("V4L2_CPU_ONLY_RESULT=%s allocations=%d passes=%d total_badbytes=%llu failed_passes=%u idle_changes=%u ram_failed=%u\n",
           total||idle_changes||ram_failed?"FAIL":"PASS",allocs,passes,total,failed,idle_changes,ram_failed);
    free(src);free(ordinary);close(f);
    return total||idle_changes||ram_failed?1:0;
}
