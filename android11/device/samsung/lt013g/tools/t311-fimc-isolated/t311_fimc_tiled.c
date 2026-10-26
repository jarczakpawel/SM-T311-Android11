#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
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

struct bufs {enum v4l2_buf_type type; unsigned n; void *p[VIDEO_MAX_PLANES]; size_t sz[VIDEO_MAX_PLANES];};
static int cmd(int fd,unsigned long op,void *ptr){int x;do{x=ioctl(fd,op,ptr);}while(x<0&&errno==EINTR);return x;}
static int error(const char *name){printf("FIMC_ERROR=%s errno=%d %s\n",name,errno,strerror(errno));return -1;}
static int setup(int fd,struct bufs *b,enum v4l2_buf_type type){
 b->type=type;
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
 struct v4l2_buffer v={.type=b->type,.memory=V4L2_MEMORY_MMAP,.length=b->n};struct v4l2_plane p[VIDEO_MAX_PLANES]={};v.m.planes=p;
 for(unsigned i=0;i<b->n;i++){p[i].length=b->sz[i];p[i].bytesused=output?b->sz[i]:0;}
 if(cmd(fd,VIDIOC_QBUF,&v)<0)return error("QBUF");
 return 0;
}
static int dequeue(int fd,struct bufs*b){
 for(int i=0;i<140;i++){
  struct v4l2_buffer v={.type=b->type,.memory=V4L2_MEMORY_MMAP,.length=b->n};struct v4l2_plane p[VIDEO_MAX_PLANES]={};v.m.planes=p;
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
int main(int argc,char **argv){
 setvbuf(stdout,NULL,_IONBF,0);
 int w=1280,h=720,frames=6;
 if(argc>=3){w=atoi(argv[1]);h=atoi(argv[2]);}if(argc>=4)frames=atoi(argv[3]);
 if(w<64||w>1920||h<64||h>1080||h%2||w%2||frames<1||frames>32)return 2;
 int fd=open("/dev/video2",O_RDWR|O_NONBLOCK|O_CLOEXEC);if(fd<0)return error("OPEN");
 int rc=1;unsigned active=0;struct bufs src={0},dst={0};
 struct v4l2_capability cap={};if(cmd(fd,VIDIOC_QUERYCAP,&cap)<0){error("QUERYCAP");goto done;}
 printf("FIMC_DRIVER=%s CARD=%s\n",cap.driver,cap.card);
 struct v4l2_format sf={.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE};
 sf.fmt.pix_mp.width=w;sf.fmt.pix_mp.height=(h==720?736:h);sf.fmt.pix_mp.pixelformat=V4L2_PIX_FMT_NV12MT;sf.fmt.pix_mp.num_planes=2;
 if(cmd(fd,VIDIOC_S_FMT,&sf)<0){error("SFMT_OUT");goto done;}
 struct v4l2_format df={.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE};
 df.fmt.pix_mp.width=w;df.fmt.pix_mp.height=h;df.fmt.pix_mp.pixelformat=V4L2_PIX_FMT_NV12;df.fmt.pix_mp.num_planes=1;
 if(cmd(fd,VIDIOC_S_FMT,&df)<0){error("SFMT_CAP");goto done;}
 if(sf.fmt.pix_mp.width!=(unsigned)w||sf.fmt.pix_mp.height!=(unsigned)(h==720?736:h)||sf.fmt.pix_mp.pixelformat!=V4L2_PIX_FMT_NV12MT||sf.fmt.pix_mp.num_planes!=2||
    df.fmt.pix_mp.width!=(unsigned)w||df.fmt.pix_mp.height!=(unsigned)h||df.fmt.pix_mp.pixelformat!=V4L2_PIX_FMT_NV12||df.fmt.pix_mp.num_planes!=1){
  puts("FIMC_FORMAT_MISMATCH");goto done;
 }
 if(h==720){struct v4l2_selection sel={};sel.type=V4L2_BUF_TYPE_VIDEO_OUTPUT;sel.target=V4L2_SEL_TGT_CROP;sel.r.left=0;sel.r.top=0;sel.r.width=w;sel.r.height=h;if(cmd(fd,VIDIOC_S_SELECTION,&sel)<0){error("CROP");goto done;}}
 unsigned systride=sf.fmt.pix_mp.plane_fmt[0].bytesperline;
 unsigned suvstride=sf.fmt.pix_mp.plane_fmt[1].bytesperline;
 unsigned dystride=df.fmt.pix_mp.plane_fmt[0].bytesperline;
 printf("FIMC_LAYOUT=%dx%d srcY=%u srcUV=%u dst=%u dstsize=%u\n",w,h,systride,suvstride,dystride,df.fmt.pix_mp.plane_fmt[0].sizeimage);
 if(setup(fd,&src,V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)<0||setup(fd,&dst,V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)<0)goto done;
 if(src.n!=2||dst.n!=1||systride<(unsigned)w||suvstride<(unsigned)w||dystride<(unsigned)w||
   src.sz[0]<(size_t)systride*h||src.sz[1]<(size_t)suvstride*h/2||dst.sz[0]<(size_t)dystride*h*3/2){
  puts("FIMC_UNEXPECTED_LAYOUT");goto done;
 }
 uint64_t badtotal=0;
 for(int k=0;k<frames;k++){
  uint8_t *sy=src.p[0],*suv=src.p[1],*dy=dst.p[0];
  memset(sy,0,src.sz[0]);memset(suv,0,src.sz[1]);memset(dy,0xc7,dst.sz[0]);
  memset(sy,44+k,src.sz[0]);
  memset(suv,135+k,src.sz[1]);
  if(queue(fd,&dst,0)<0||queue(fd,&src,1)<0)goto done;
  enum v4l2_buf_type ct=dst.type,ot=src.type;
  if(cmd(fd,VIDIOC_STREAMON,&ct)<0){error("STREAMON_CAP");goto done;}active|=1;
  if(cmd(fd,VIDIOC_STREAMON,&ot)<0){error("STREAMON_OUT");goto done;}active|=2;
  if(dequeue(fd,&dst)<0||dequeue(fd,&src)<0)goto done;
  if(cmd(fd,VIDIOC_STREAMOFF,&ot)<0){error("STREAMOFF_OUT");goto done;}active&=~2;
  if(cmd(fd,VIDIOC_STREAMOFF,&ct)<0){error("STREAMOFF_CAP");goto done;}active&=~1;
  uint64_t wrong=0;int fx=-1,fy=-1,fp=-1;
  for(int y=0;y<h;y++)for(int x=0;x<w;x++)if(dy[(size_t)y*dystride+x]!=(uint8_t)(44+k)){if(!wrong){fx=x;fy=y;fp=0;}wrong++;}
  for(int y=0;y<h/2;y++)for(int x=0;x<w;x++)if(dy[(size_t)(h+y)*dystride+x]!=(uint8_t)(135+k)){if(!wrong){fx=x;fy=y;fp=1;}wrong++;}
  printf("FIMC_FRAME=%d badbytes=%llu firstplane=%d firstx=%d firsty=%d\n",k,(unsigned long long)wrong,fp,fx,fy);
  badtotal+=wrong;
 }
 printf("FIMC_ISOLATED_RESULT=%s frames=%d badbytes=%llu\n",badtotal?"FAIL":"PASS",frames,(unsigned long long)badtotal);
 rc=badtotal?3:0;
done:
 if(active&2){enum v4l2_buf_type t=src.type;cmd(fd,VIDIOC_STREAMOFF,&t);}
 if(active&1){enum v4l2_buf_type t=dst.type;cmd(fd,VIDIOC_STREAMOFF,&t);}
 unmap_buf(&src);unmap_buf(&dst);close(fd);return rc;
}
