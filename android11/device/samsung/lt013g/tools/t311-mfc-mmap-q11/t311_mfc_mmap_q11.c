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

#define NODE "/dev/video7"
#define W 640
#define H 480
#define CAP_COUNT 3
#define SRC_COUNT 2

struct map_plane { void *ptr; size_t len; };
struct map_buf { unsigned int nplanes; struct map_plane p[VIDEO_MAX_PLANES]; };

static int xioctl(int fd, unsigned long req, void *arg, const char *name) {
    int r; do { r=ioctl(fd,req,arg); } while (r<0 && errno==EINTR);
    if (r<0) fprintf(stderr,"Q11_IOCTL_FAIL name=%s errno=%d(%s)\n",name,errno,strerror(errno));
    else printf("Q11_IOCTL_PASS name=%s\n",name);
    fflush(stdout); fflush(stderr); return r;
}
static int sctrl(int fd,uint32_t id,int32_t val,const char *name){ struct v4l2_control c={.id=id,.value=val}; int r=ioctl(fd,VIDIOC_S_CTRL,&c); printf("Q11_CTRL name=%s rc=%d errno=%d\n",name,r,r<0?errno:0); return r; }
static int setup_formats(int fd){
    struct v4l2_format c,o; memset(&c,0,sizeof(c)); c.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE; c.fmt.pix_mp.width=W; c.fmt.pix_mp.height=H; c.fmt.pix_mp.pixelformat=V4L2_PIX_FMT_H264; c.fmt.pix_mp.num_planes=1; c.fmt.pix_mp.plane_fmt[0].sizeimage=2*1024*1024;
    if(xioctl(fd,VIDIOC_S_FMT,&c,"S_FMT_CAPTURE_H264")<0)return -1;
    printf("Q11_FMT_CAPTURE got=%ux%u size=%u bpl=%u\n",c.fmt.pix_mp.width,c.fmt.pix_mp.height,c.fmt.pix_mp.plane_fmt[0].sizeimage,c.fmt.pix_mp.plane_fmt[0].bytesperline);
    memset(&o,0,sizeof(o)); o.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE; o.fmt.pix_mp.width=W; o.fmt.pix_mp.height=H; o.fmt.pix_mp.pixelformat=V4L2_PIX_FMT_NV12M; o.fmt.pix_mp.num_planes=2;
    if(xioctl(fd,VIDIOC_S_FMT,&o,"S_FMT_OUTPUT_NV12M")<0)return -1;
    printf("Q11_FMT_OUTPUT got=%ux%u y_bpl=%u y_size=%u uv_bpl=%u uv_size=%u\n",o.fmt.pix_mp.width,o.fmt.pix_mp.height,o.fmt.pix_mp.plane_fmt[0].bytesperline,o.fmt.pix_mp.plane_fmt[0].sizeimage,o.fmt.pix_mp.plane_fmt[1].bytesperline,o.fmt.pix_mp.plane_fmt[1].sizeimage);
#ifdef V4L2_CID_MPEG_VIDEO_HEADER_MODE
    sctrl(fd,V4L2_CID_MPEG_VIDEO_HEADER_MODE,V4L2_MPEG_VIDEO_HEADER_MODE_SEPARATE,"HEADER_SEPARATE");
#endif
#ifdef V4L2_CID_MPEG_VIDEO_BITRATE
    sctrl(fd,V4L2_CID_MPEG_VIDEO_BITRATE,2000000,"BITRATE");
#endif
#ifdef V4L2_CID_MPEG_VIDEO_GOP_SIZE
    sctrl(fd,V4L2_CID_MPEG_VIDEO_GOP_SIZE,30,"GOP");
#endif
#ifdef V4L2_CID_MPEG_VIDEO_B_FRAMES
    sctrl(fd,V4L2_CID_MPEG_VIDEO_B_FRAMES,0,"B_FRAMES");
#endif
    fflush(stdout); return 0;
}
static int alloc_mmap(int fd,enum v4l2_buf_type type,unsigned int count,struct map_buf *m,unsigned int *actual){
    struct v4l2_requestbuffers rb; memset(&rb,0,sizeof(rb)); rb.type=type; rb.memory=V4L2_MEMORY_MMAP; rb.count=count;
    if(xioctl(fd,VIDIOC_REQBUFS,&rb,type==V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE?"REQBUFS_CAPTURE_MMAP":"REQBUFS_OUTPUT_MMAP")<0)return -1;
    printf("Q11_REQBUFS type=%u actual=%u\n",type,rb.count); if(!rb.count)return -1; *actual=rb.count;
    for(unsigned i=0;i<rb.count;i++){ struct v4l2_buffer b; struct v4l2_plane p[VIDEO_MAX_PLANES]; memset(&b,0,sizeof(b)); memset(p,0,sizeof(p)); b.type=type;b.memory=V4L2_MEMORY_MMAP;b.index=i;b.length=VIDEO_MAX_PLANES;b.m.planes=p; if(xioctl(fd,VIDIOC_QUERYBUF,&b,"QUERYBUF_MMAP")<0)return -1; m[i].nplanes=b.length; for(unsigned j=0;j<b.length;j++){m[i].p[j].len=p[j].length;m[i].p[j].ptr=mmap(NULL,p[j].length,PROT_READ|PROT_WRITE,MAP_SHARED,fd,p[j].m.mem_offset);printf("Q11_MMAP type=%u idx=%u plane=%u len=%u off=0x%x ptr=%p\n",type,i,j,p[j].length,p[j].m.mem_offset,m[i].p[j].ptr);if(m[i].p[j].ptr==MAP_FAILED)return -1;}}
    return 0;
}
static int qbuf_idx(int fd,enum v4l2_buf_type type,unsigned idx,const struct map_buf *m,int raw){ struct v4l2_buffer b;struct v4l2_plane p[VIDEO_MAX_PLANES];memset(&b,0,sizeof(b));memset(p,0,sizeof(p));b.type=type;b.memory=V4L2_MEMORY_MMAP;b.index=idx;b.length=m->nplanes;b.m.planes=p;for(unsigned j=0;j<m->nplanes;j++){p[j].length=m->p[j].len;if(raw)p[j].bytesused=(j==0)?W*H:W*H/2;}return xioctl(fd,VIDIOC_QBUF,&b,type==V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE?"QBUF_CAPTURE":"QBUF_OUTPUT"); }
static int dqcap(int fd,int ms,struct v4l2_buffer *b,struct v4l2_plane *p){for(int e=0;e<ms;e+=10){memset(b,0,sizeof(*b));memset(p,0,sizeof(struct v4l2_plane)*VIDEO_MAX_PLANES);b->type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;b->memory=V4L2_MEMORY_MMAP;b->length=VIDEO_MAX_PLANES;b->m.planes=p;if(ioctl(fd,VIDIOC_DQBUF,b)==0){printf("Q11_DQ_CAPTURE idx=%u seq=%u bytes=%u\n",b->index,b->sequence,p[0].bytesused);return 0;}if(errno!=EAGAIN&&errno!=EINTR){fprintf(stderr,"Q11_DQ_FAIL errno=%d(%s)\n",errno,strerror(errno));return -1;}usleep(10000);}printf("Q11_DQ_TIMEOUT ms=%d\n",ms);return 1;}
static void unmap_all(struct map_buf *m,unsigned n){for(unsigned i=0;i<n;i++)for(unsigned j=0;j<m[i].nplanes;j++)if(m[i].p[j].ptr&&m[i].p[j].ptr!=MAP_FAILED)munmap(m[i].p[j].ptr,m[i].p[j].len);}
int main(int argc,char **argv){const char *stage=argc>1?argv[1]:"full";printf("Q11_DIRECT_BEGIN stage=%s\n",stage);fflush(stdout);int fd=open(NODE,O_RDWR|O_NONBLOCK);if(fd<0){perror("open");return 2;}struct v4l2_capability q;memset(&q,0,sizeof(q));if(xioctl(fd,VIDIOC_QUERYCAP,&q,"QUERYCAP")<0)return 3;if(setup_formats(fd)<0)return 4;if(!strcmp(stage,"fmt")){printf("Q11_DIRECT_RESULT=PASS stage=fmt\n");close(fd);return 0;}
    struct map_buf cap[CAP_COUNT],src[SRC_COUNT];memset(cap,0,sizeof(cap));memset(src,0,sizeof(src));unsigned nc=0,ns=0;if(alloc_mmap(fd,V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,CAP_COUNT,cap,&nc)<0)return 5;for(unsigned i=0;i<nc;i++)if(qbuf_idx(fd,V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,i,&cap[i],0)<0)return 6;enum v4l2_buf_type ct=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;if(xioctl(fd,VIDIOC_STREAMON,&ct,"STREAMON_CAPTURE")<0)return 7;struct v4l2_buffer hb;struct v4l2_plane hp[VIDEO_MAX_PLANES];int hr=dqcap(fd,5000,&hb,hp);if(hr==0){printf("Q11_HEADER bytes=%u\n",hp[0].bytesused);qbuf_idx(fd,V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,hb.index,&cap[hb.index],0);}else printf("Q11_HEADER_NOT_DEQUEUED rc=%d\n",hr);if(!strcmp(stage,"cap")){printf("Q11_DIRECT_RESULT=PASS stage=cap header_rc=%d\n",hr);ioctl(fd,VIDIOC_STREAMOFF,&ct);unmap_all(cap,nc);close(fd);return 0;}
    if(alloc_mmap(fd,V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,SRC_COUNT,src,&ns)<0)return 8;memset(src[0].p[0].ptr,0x10,src[0].p[0].len);memset(src[0].p[1].ptr,0x80,src[0].p[1].len);printf("Q11_SOURCE_FILLED y=%zu uv=%zu\n",src[0].p[0].len,src[0].p[1].len);if(qbuf_idx(fd,V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,0,&src[0],1)<0)return 9;enum v4l2_buf_type ot=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;printf("Q11_DANGEROUS_BOUNDARY=BEFORE_STREAMON_OUTPUT\n");fflush(stdout);if(xioctl(fd,VIDIOC_STREAMON,&ot,"STREAMON_OUTPUT")<0)return 10;printf("Q11_DANGEROUS_BOUNDARY=AFTER_STREAMON_OUTPUT\n");fflush(stdout);struct v4l2_buffer ob;struct v4l2_plane op[VIDEO_MAX_PLANES];int er=dqcap(fd,8000,&ob,op);if(er!=0){printf("Q11_DIRECT_RESULT=FAIL stage=full reason=NO_CAPTURE rc=%d\n",er);return 11;}unsigned payload=op[0].bytesused;printf("Q11_ENCODED_FRAME bytes=%u\n",payload);if(payload&&ob.index<nc){int of=open("/data/local/tmp/q11-direct.h264",O_CREAT|O_TRUNC|O_WRONLY,0644);if(of>=0){write(of,cap[ob.index].p[0].ptr,payload);close(of);}}ioctl(fd,VIDIOC_STREAMOFF,&ot);ioctl(fd,VIDIOC_STREAMOFF,&ct);unmap_all(src,ns);unmap_all(cap,nc);close(fd);if(!payload)return 12;printf("Q11_DIRECT_RESULT=PASS stage=full bytes=%u\n",payload);return 0;}
