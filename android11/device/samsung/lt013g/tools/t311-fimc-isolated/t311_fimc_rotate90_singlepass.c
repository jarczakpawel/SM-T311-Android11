#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <xf86drm.h>
#include <exynos_drm.h>
#include <drm_mode.h>
#include <linux/dma-buf.h>
#ifndef V4L2_PIX_FMT_NV12MT
#define V4L2_PIX_FMT_NV12MT v4l2_fourcc(0x54,0x4d,0x31,0x32)
#endif
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

static int g_drm=-1,g_prime=-1;static uint32_t g_handle=0;static void*g_map=NULL;static size_t g_map_sz=0;static uint64_t g_map_offset=0;
struct bufs {enum v4l2_memory memory;enum v4l2_buf_type type; unsigned n; void *p[VIDEO_MAX_PLANES]; size_t sz[VIDEO_MAX_PLANES];};
static int cmd(int fd,unsigned long op,void *ptr){int x;do{x=ioctl(fd,op,ptr);}while(x<0&&errno==EINTR);return x;}
static int error(const char *name){printf("FIMC_ERROR=%s errno=%d %s\n",name,errno,strerror(errno));return -1;}
static int setup(int fd,struct bufs *b,enum v4l2_buf_type type){
 b->type=type;b->memory=V4L2_MEMORY_MMAP;
 struct v4l2_requestbuffers r={.count=1,.type=type,.memory=V4L2_MEMORY_MMAP};
 if(cmd(fd,VIDIOC_REQBUFS,&r)<0 || r.count<1)return error("REQBUFS");
 struct v4l2_buffer v={.type=type,.memory=V4L2_MEMORY_MMAP,.length=VIDEO_MAX_PLANES};
 struct v4l2_plane planes[VIDEO_MAX_PLANES]={};v.m.planes=planes;
 if(cmd(fd,VIDIOC_QUERYBUF,&v)<0)return error("QUERYBUF");
 b->n=v.length;
 if(b->n==0||b->n>VIDEO_MAX_PLANES)return error("NUM_PLANES");
 for(unsigned k=0;k<b->n;k++){
   b->sz[k]=planes[k].length;
   b->p[k]=mmap(NULL,b->sz[k],PROT_READ|PROT_WRITE,MAP_SHARED,fd,planes[k].m.mem_offset);
   if(b->p[k]==MAP_FAILED)return error("MMAP");
 }
 return 0;
}
static int queue(int fd,struct bufs*b,int output){
 struct v4l2_buffer v={.type=b->type,.memory=b->memory,.length=b->n};struct v4l2_plane p[VIDEO_MAX_PLANES]={};v.m.planes=p;
 for(unsigned i=0;i<b->n;i++){p[i].length=b->sz[i];p[i].bytesused=output?b->sz[i]:0;if(b->memory==V4L2_MEMORY_DMABUF)p[i].m.fd=g_prime;}
 if(cmd(fd,VIDIOC_QBUF,&v)<0)return error("QBUF");
 return 0;
}
static int dequeue(int fd,struct bufs*b){
 for(int i=0;i<140;i++){
  struct v4l2_buffer v={.type=b->type,.memory=b->memory,.length=b->n};struct v4l2_plane p[VIDEO_MAX_PLANES]={};v.m.planes=p;
  if(cmd(fd,VIDIOC_DQBUF,&v)==0)return (v.flags&V4L2_BUF_FLAG_ERROR)?error("BUF_ERROR"):0;
  if(errno!=EAGAIN)return error("DQBUF");
  struct pollfd pf={.fd=fd,.events=POLLIN|POLLOUT|POLLERR};int rv=poll(&pf,1,25);
  if(rv<0&&errno!=EINTR)return error("POLL");
 }
 errno=ETIMEDOUT;return error("TIMEOUT");
}
static void unmap_buf(struct bufs*b){
 for(unsigned k=0;k<b->n;k++)if(b->p[k]&&b->p[k]!=MAP_FAILED)munmap(b->p[k],b->sz[k]);
}
static int cpu_sync(unsigned long long flags){
 struct dma_buf_sync sync={.flags=flags};
 if(ioctl(g_prime,DMA_BUF_IOCTL_SYNC,&sync)<0)return error("DMA_BUF_IOCTL_SYNC");
 return 0;
}
static int alloc_dst(int f, struct bufs *d, size_t sz){
 g_drm=open("/dev/dri/card0",O_RDWR|O_CLOEXEC);
 if(g_drm<0)return error("DRM_OPEN");
 struct drm_exynos_gem_create cr={};
 const char *pad=getenv("T311_GEM_EXTRA");
 unsigned long extra=pad?strtoul(pad,NULL,10):0;
 if(extra>65536){puts("GEM_EXTRA_OUT_OF_RANGE");return -1;}
 cr.size=sz+extra;
 const char *v=getenv("T311_GEM_FLAGS");char *end=NULL;unsigned long flags=v?strtoul(v,&end,0):8;
 if((v&&(!*v||*end))||(flags!=0&&flags!=8&&flags!=16&&flags!=24)){puts("INVALID_GEM_FLAGS");return -1;}
 cr.flags=flags;
 if(drmIoctl(g_drm,DRM_IOCTL_EXYNOS_GEM_CREATE,&cr)<0)return error("DRM_GEM_CREATE");
 g_handle=cr.handle;
 if(drmPrimeHandleToFD(g_drm,g_handle,DRM_CLOEXEC|DRM_RDWR,&g_prime)<0)return error("DRM_PRIME_EXPORT");
 struct drm_mode_map_dumb m={};m.handle=g_handle;
 if(drmIoctl(g_drm,DRM_IOCTL_MODE_MAP_DUMB,&m)<0)return error("DRM_MAP_DUMB");
 g_map_sz=cr.size;g_map_offset=m.offset;
 if(!getenv("T311_LATE_MMAP")){
  g_map=mmap(NULL,g_map_sz,PROT_READ|PROT_WRITE,MAP_SHARED,g_drm,g_map_offset);
  if(g_map==MAP_FAILED)return error("DRM_MMAP");
 }
 struct v4l2_requestbuffers r={.count=1,.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,.memory=V4L2_MEMORY_DMABUF};
 if(cmd(f,VIDIOC_REQBUFS,&r)<0||r.count<1)return error("CAPTURE_REQBUFS_DMABUF");
 d->type=r.type;d->memory=r.memory;d->n=1;d->sz[0]=sz;
 printf("FIMC_DMABUF_ALLOC visible=%zu actual=%llu extra=%lu flags=0x%x prime_fd=%d\n",sz,(unsigned long long)cr.size,extra,cr.flags,g_prime);
 return 0;
}
int main(int argc,char **argv){
 setvbuf(stdout,NULL,_IONBF,0);
 int w=640,h=480,frames=1;
 if(argc>=3){w=atoi(argv[1]);h=atoi(argv[2]);}if(argc>=4)frames=atoi(argv[3]);
 if(w<64||w>1280||h<64||h>720||h%16||w%16||frames!=1)return 2;
 int fd=open("/dev/video2",O_RDWR|O_NONBLOCK|O_CLOEXEC);if(fd<0)return error("OPEN");
 int rc=1;unsigned active=0;struct bufs src={0},dst={0};
 struct v4l2_capability cap={};if(cmd(fd,VIDIOC_QUERYCAP,&cap)<0){error("QUERYCAP");goto done;}
 printf("FIMC_DRIVER=%s CARD=%s\n",cap.driver,cap.card);
 struct v4l2_format sf={.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE};
 int linear=1;
 sf.fmt.pix_mp.width=w;sf.fmt.pix_mp.height=linear?h:(h==720?736:(h==1080?1088:h));sf.fmt.pix_mp.pixelformat=linear?V4L2_PIX_FMT_NV12M:V4L2_PIX_FMT_NV12MT;sf.fmt.pix_mp.num_planes=2;
 if(cmd(fd,VIDIOC_S_FMT,&sf)<0){error("SFMT_OUT");goto done;}
 struct v4l2_format df={.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE};
 df.fmt.pix_mp.width=h;df.fmt.pix_mp.height=w;df.fmt.pix_mp.pixelformat=V4L2_PIX_FMT_NV12;df.fmt.pix_mp.num_planes=1;
 if(cmd(fd,VIDIOC_S_FMT,&df)<0){error("SFMT_CAP");goto done;}
 if(sf.fmt.pix_mp.width!=(unsigned)w||sf.fmt.pix_mp.height!=(unsigned)(linear?h:(h==720?736:(h==1080?1088:h)))||sf.fmt.pix_mp.pixelformat!=(linear?V4L2_PIX_FMT_NV12M:V4L2_PIX_FMT_NV12MT)||sf.fmt.pix_mp.num_planes!=2||
    df.fmt.pix_mp.width!=(unsigned)h||df.fmt.pix_mp.height!=(unsigned)w||df.fmt.pix_mp.pixelformat!=V4L2_PIX_FMT_NV12||df.fmt.pix_mp.num_planes!=1){
  puts("FIMC_FORMAT_MISMATCH");goto done;
 }
 if(!linear&&(h==720||h==1080)){struct v4l2_selection sel={};sel.type=V4L2_BUF_TYPE_VIDEO_OUTPUT;sel.target=V4L2_SEL_TGT_CROP;sel.r.left=0;sel.r.top=0;sel.r.width=w;sel.r.height=h;if(cmd(fd,VIDIOC_S_SELECTION,&sel)<0){error("CROP");goto done;}}
 struct v4l2_control rot={.id=V4L2_CID_ROTATE,.value=90};
 if(cmd(fd,VIDIOC_S_CTRL,&rot)<0){error("ROTATE90_CTRL");goto done;}
 printf("FIMC_ROTATE90_CONTROL=PASS\n");
 unsigned systride=sf.fmt.pix_mp.plane_fmt[0].bytesperline;
 unsigned suvstride=sf.fmt.pix_mp.plane_fmt[1].bytesperline;
 unsigned dystride=df.fmt.pix_mp.plane_fmt[0].bytesperline;
 printf("FIMC_LAYOUT=%dx%d srcY=%u srcUV=%u dst=%u dstsize=%u\n",w,h,systride,suvstride,dystride,df.fmt.pix_mp.plane_fmt[0].sizeimage);
 if(setup(fd,&src,V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)<0||alloc_dst(fd,&dst,df.fmt.pix_mp.plane_fmt[0].sizeimage)<0)goto done;
 if(src.n!=2||dst.n!=1||systride<(unsigned)w||suvstride<(unsigned)w||dystride<(unsigned)h||
   src.sz[0]<(size_t)systride*h||src.sz[1]<(size_t)suvstride*h/2||dst.sz[0]<(size_t)dystride*w*3/2){
  puts("FIMC_UNEXPECTED_LAYOUT");goto done;
 }
 uint64_t badtotal=0;
 for(int k=0;k<frames;k++){
  uint8_t *sy=src.p[0],*suv=src.p[1],*dy=g_map;
  memset(sy,0,src.sz[0]);memset(suv,0,src.sz[1]);if(!getenv("T311_NO_PRECLEAR")){if(!dy){puts("LATE_MAP_REQUIRES_NO_PRECLEAR");goto done;}
   if(getenv("T311_DMA_SYNC")&&cpu_sync(DMA_BUF_SYNC_START|DMA_BUF_SYNC_WRITE)<0)goto done;
   memset(dy,0xc7,dst.sz[0]);
   if(getenv("T311_DMA_SYNC")&&cpu_sync(DMA_BUF_SYNC_END|DMA_BUF_SYNC_WRITE)<0)goto done;
  }
  for(int y=0;y<h;y++)for(int x=0;x<w;x++)sy[(size_t)y*systride+x]=(uint8_t)(x*7+y*11+33);
  for(int y=0;y<h/2;y++)for(int x=0;x<w/2;x++) {
   suv[(size_t)y*suvstride+x*2]=(uint8_t)((x/16)*13+(y/16)*5+17);
   suv[(size_t)y*suvstride+x*2+1]=(uint8_t)((x/16)*3+(y/16)*19+77);
  }
  __asm__ __volatile__("dsb sy" ::: "memory");
  if(queue(fd,&dst,0)<0||queue(fd,&src,1)<0)goto done;
  enum v4l2_buf_type ct=dst.type,ot=src.type;
  if(cmd(fd,VIDIOC_STREAMON,&ct)<0){error("STREAMON_CAP");goto done;}active|=1;
  if(cmd(fd,VIDIOC_STREAMON,&ot)<0){error("STREAMON_OUT");goto done;}active|=2;
  if(dequeue(fd,&dst)<0||dequeue(fd,&src)<0)goto done;__asm__ __volatile__("dsb sy" ::: "memory");
  if(cmd(fd,VIDIOC_STREAMOFF,&ot)<0){error("STREAMOFF_OUT");goto done;}active&=~2;
  if(cmd(fd,VIDIOC_STREAMOFF,&ct)<0){error("STREAMOFF_CAP");goto done;}active&=~1;
  if(getenv("T311_LATE_MMAP")){
   g_map=mmap(NULL,g_map_sz,PROT_READ|PROT_WRITE,MAP_SHARED,g_drm,g_map_offset);
   if(g_map==MAP_FAILED){error("LATE_MMAP");goto done;}
   dy=g_map;
  }
  if(getenv("T311_DMA_SYNC")&&cpu_sync(DMA_BUF_SYNC_START|DMA_BUF_SYNC_READ)<0)goto done;
  uint64_t wrong=0,ccw=0;
  for(int y=0;y<w;y++)for(int x=0;x<h;x++) {
   const uint8_t got=dy[(size_t)y*dystride+x];
   const uint8_t cw=sy[(size_t)(h-1-x)*systride+y];
   const uint8_t anti=sy[(size_t)x*systride+(w-1-y)];
   wrong+=(got!=cw);ccw+=(got!=anti);
  }
  uint64_t uv_wrong=0,uv_anti_wrong=0,uv_non_sentinel=0,uv_interior_bad=0,uv_interior_total=0;
  const size_t dst_uv=(size_t)dystride*w;
  for(int y=0;y<w/2;y++)for(int x=0;x<h/2;x++)for(int c=0;c<2;c++) {
   const uint8_t got=dy[dst_uv+(size_t)y*dystride+x*2+c];
   const uint8_t cw=suv[(size_t)(h/2-1-x)*suvstride+y*2+c];
   const uint8_t anti=suv[(size_t)x*suvstride+(w/2-1-y)*2+c];
   uv_non_sentinel+=(got!=0xc7);
   const int sx=y,sy=h/2-1-x;
   if(sx%16>=4 && sx%16<12 && sy%16>=4 && sy%16<12) {
    uv_interior_total++;
    uv_interior_bad+=(got!=cw);
   }
   if(y==0 && x<4)printf("CHROMA_SAMPLE_PAIR=%d BYTE=%d GOT=0x%02x CW=0x%02x CCW=0x%02x\n",x,c,got,cw,anti);
   uv_wrong+=(got!=cw);uv_anti_wrong+=(got!=anti);
  }
  if(getenv("T311_DMA_SYNC")&&cpu_sync(DMA_BUF_SYNC_END|DMA_BUF_SYNC_READ)<0)goto done;
  printf("FIMC_ROTATE90_Y_CW_ERRORS=%llu Y_CCW_ERRORS=%llu UV_CW_ERRORS=%llu UV_CCW_ERRORS=%llu\n",
   (unsigned long long)wrong,(unsigned long long)ccw,(unsigned long long)uv_wrong,(unsigned long long)uv_anti_wrong);
  printf("CHROMA_NON_SENTINEL=%llu OF=%u\n",(unsigned long long)uv_non_sentinel,w*h/2);
  printf("FIMC_NV12_BLOCK_CHROMA_INTERIOR_BYTES=%llu INTERIOR_ERRORS=%llu\n",(unsigned long long)uv_interior_total,(unsigned long long)uv_interior_bad);
  badtotal+=wrong+uv_interior_bad;
  if(getenv("T311_LATE_MMAP")){munmap(g_map,g_map_sz);g_map=NULL;}
 }
 printf("FIMC_ISOLATED_RESULT=%s frames=%d badbytes=%llu\n",badtotal?"FAIL":"PASS",frames,(unsigned long long)badtotal);
 rc=badtotal?3:0;
done:
 if(active&2){enum v4l2_buf_type t=src.type;cmd(fd,VIDIOC_STREAMOFF,&t);}
 if(active&1){enum v4l2_buf_type t=dst.type;cmd(fd,VIDIOC_STREAMOFF,&t);}
 unmap_buf(&src);unmap_buf(&dst);
 if(g_map && g_map!=MAP_FAILED)munmap(g_map,g_map_sz);
 if(g_prime>=0)close(g_prime);
 if(g_drm>=0){if(g_handle){struct drm_gem_close c={.handle=g_handle};drmIoctl(g_drm,DRM_IOCTL_GEM_CLOSE,&c);}close(g_drm);}
 close(fd);return rc;
}
