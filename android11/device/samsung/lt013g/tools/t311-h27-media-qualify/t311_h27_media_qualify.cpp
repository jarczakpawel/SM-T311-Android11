#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <cstdint>
#include <chrono>

static int xioctl(int fd, unsigned long req, void* arg) {
    int r; do { r=ioctl(fd,req,arg); } while(r<0 && errno==EINTR); return r;
}
static int cmdprobe() {
    int fail=0;
    int e=open("/dev/video7",O_RDWR|O_CLOEXEC);
    if(e<0){perror("open video7"); return 2;}
    for(unsigned c: {V4L2_ENC_CMD_STOP,V4L2_ENC_CMD_START}) {
        v4l2_encoder_cmd x{}; x.cmd=c; errno=0; int r=xioctl(e,VIDIOC_TRY_ENCODER_CMD,&x);
        printf("T311_MFC_TRY_ENCODER_CMD cmd=%u rc=%d errno=%d\n",c,r,r?errno:0); if(r) fail++;
    }
    close(e);
    int d=open("/dev/video6",O_RDWR|O_CLOEXEC);
    if(d<0){perror("open video6"); return 2;}
    for(unsigned c: {V4L2_DEC_CMD_STOP,V4L2_DEC_CMD_START}) {
        v4l2_decoder_cmd x{}; x.cmd=c; errno=0; int r=xioctl(d,VIDIOC_TRY_DECODER_CMD,&x);
        printf("T311_MFC_TRY_DECODER_CMD cmd=%u rc=%d errno=%d\n",c,r,r?errno:0); if(r) fail++;
    }
    close(d);
    printf("T311_MFC_CMDPROBE=%s\n",fail?"FAIL":"PASS");
    return fail?3:0;
}
struct Map { void* p=MAP_FAILED; size_t n=0; };
static bool alloc_map(int fd, v4l2_buf_type type, Map* m, v4l2_buffer* b) {
    v4l2_requestbuffers req{}; req.type=type; req.memory=V4L2_MEMORY_MMAP; req.count=1;
    if(xioctl(fd,VIDIOC_REQBUFS,&req)<0 || req.count<1) return false;
    memset(b,0,sizeof(*b)); b->type=type; b->memory=V4L2_MEMORY_MMAP; b->index=0;
    if(xioctl(fd,VIDIOC_QUERYBUF,b)<0) return false;
    m->n=b->length; m->p=mmap(nullptr,m->n,PROT_READ|PROT_WRITE,MAP_SHARED,fd,b->m.offset);
    return m->p!=MAP_FAILED;
}
static int dq_capture_timeout(int fd, v4l2_buffer* b, int timeout_ms) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout_ms);
    for(;;) {
        if(xioctl(fd,VIDIOC_DQBUF,b)==0) return 0;
        if(errno!=EAGAIN && errno!=EWOULDBLOCK) return -1;
        const auto now=std::chrono::steady_clock::now();
        if(now>=deadline) { errno=ETIMEDOUT; return -1; }
        int remain=(int)std::chrono::duration_cast<std::chrono::milliseconds>(deadline-now).count();
        if(remain>250) remain=250;
        pollfd pfd{fd,POLLIN|POLLPRI,0};
        int r=poll(&pfd,1,remain);
        if(r<0 && errno!=EINTR) return -1;
        if(r>0 && !(pfd.revents&(POLLIN|POLLPRI))) usleep(10000);
    }
}
static int jpegenc(const char* path,int w,int h) {
    int fd=open("/dev/video4",O_RDWR|O_CLOEXEC|O_NONBLOCK); if(fd<0){perror("open video4");return 2;}
    v4l2_format out{}; out.type=V4L2_BUF_TYPE_VIDEO_OUTPUT; out.fmt.pix.width=w; out.fmt.pix.height=h;
    out.fmt.pix.pixelformat=V4L2_PIX_FMT_YUV420; out.fmt.pix.field=V4L2_FIELD_NONE;
    if(xioctl(fd,VIDIOC_S_FMT,&out)<0){perror("jpeg S_FMT output");close(fd);return 3;}
    v4l2_format cap{}; cap.type=V4L2_BUF_TYPE_VIDEO_CAPTURE; cap.fmt.pix.width=w; cap.fmt.pix.height=h;
    cap.fmt.pix.pixelformat=V4L2_PIX_FMT_JPEG; cap.fmt.pix.field=V4L2_FIELD_NONE;
    if(xioctl(fd,VIDIOC_S_FMT,&cap)<0){perror("jpeg S_FMT capture");close(fd);return 4;}
    v4l2_control q{}; q.id=V4L2_CID_JPEG_COMPRESSION_QUALITY; q.value=90; xioctl(fd,VIDIOC_S_CTRL,&q);
    Map mo,mc; v4l2_buffer bo{},bc{};
    if(!alloc_map(fd,V4L2_BUF_TYPE_VIDEO_OUTPUT,&mo,&bo)||!alloc_map(fd,V4L2_BUF_TYPE_VIDEO_CAPTURE,&mc,&bc)) {perror("jpeg mmap");close(fd);return 5;}
    memset(mo.p,0x80,mo.n); // neutral synthetic YUV420
    bo.bytesused=out.fmt.pix.sizeimage ? out.fmt.pix.sizeimage : (unsigned)(w*h*3/2);
    if(bo.bytesused>mo.n) bo.bytesused=mo.n;
    if(xioctl(fd,VIDIOC_QBUF,&bc)<0||xioctl(fd,VIDIOC_QBUF,&bo)<0){perror("jpeg qbuf");return 6;}
    v4l2_buf_type tc=V4L2_BUF_TYPE_VIDEO_CAPTURE,to=V4L2_BUF_TYPE_VIDEO_OUTPUT;
    printf("T311_JPEG_ENC_STAGE=streamon\n");
    if(xioctl(fd,VIDIOC_STREAMON,&tc)<0||xioctl(fd,VIDIOC_STREAMON,&to)<0){perror("jpeg streamon");return 7;}
    memset(&bc,0,sizeof(bc)); bc.type=V4L2_BUF_TYPE_VIDEO_CAPTURE; bc.memory=V4L2_MEMORY_MMAP;
    printf("T311_JPEG_ENC_STAGE=wait_capture\n");
    if(dq_capture_timeout(fd,&bc,5000)<0){perror("jpeg dq cap timeout");return 9;}
    FILE* f=fopen(path,"wb"); if(!f)return 10; fwrite(mc.p,1,bc.bytesused,f); fclose(f);
    bool soi=bc.bytesused>2 && ((unsigned char*)mc.p)[0]==0xff && ((unsigned char*)mc.p)[1]==0xd8;
    printf("T311_JPEG_ENC bytes=%u soi=%d fmt=%c%c%c%c\n",bc.bytesused,soi,
      cap.fmt.pix.pixelformat&255,(cap.fmt.pix.pixelformat>>8)&255,(cap.fmt.pix.pixelformat>>16)&255,(cap.fmt.pix.pixelformat>>24)&255);
    xioctl(fd,VIDIOC_STREAMOFF,&to); xioctl(fd,VIDIOC_STREAMOFF,&tc);
    munmap(mo.p,mo.n); munmap(mc.p,mc.n); close(fd);
    printf("T311_JPEG_ENC_RESULT=%s\n",(soi&&bc.bytesused>128)?"PASS":"FAIL");
    return (soi&&bc.bytesused>128)?0:11;
}
static int jpegdec(const char* inpath,const char* outpath,int w,int h) {
    FILE* fi=fopen(inpath,"rb"); if(!fi)return 2; fseek(fi,0,SEEK_END); long sz=ftell(fi); rewind(fi); if(sz<=0){fclose(fi);return 3;}
    std::vector<unsigned char> jpg(sz); if(fread(jpg.data(),1,jpg.size(),fi)!=jpg.size()){fclose(fi);return 4;} fclose(fi);
    int fd=open("/dev/video5",O_RDWR|O_CLOEXEC|O_NONBLOCK); if(fd<0){perror("open video5");return 5;}
    v4l2_format out{}; out.type=V4L2_BUF_TYPE_VIDEO_OUTPUT; out.fmt.pix.width=w; out.fmt.pix.height=h; out.fmt.pix.pixelformat=V4L2_PIX_FMT_JPEG; out.fmt.pix.field=V4L2_FIELD_NONE; out.fmt.pix.sizeimage=jpg.size();
    if(xioctl(fd,VIDIOC_S_FMT,&out)<0){perror("dec S_FMT output");return 6;}
    v4l2_format cap{}; cap.type=V4L2_BUF_TYPE_VIDEO_CAPTURE; cap.fmt.pix.width=w; cap.fmt.pix.height=h; cap.fmt.pix.pixelformat=V4L2_PIX_FMT_YUV420; cap.fmt.pix.field=V4L2_FIELD_NONE;
    if(xioctl(fd,VIDIOC_S_FMT,&cap)<0){perror("dec S_FMT capture");return 7;}
    Map mo,mc; v4l2_buffer bo{},bc{}; if(!alloc_map(fd,V4L2_BUF_TYPE_VIDEO_OUTPUT,&mo,&bo)||!alloc_map(fd,V4L2_BUF_TYPE_VIDEO_CAPTURE,&mc,&bc))return 8;
    if(jpg.size()>mo.n){fprintf(stderr,"jpeg input buffer too small %zu>%zu\n",jpg.size(),mo.n);return 9;} memcpy(mo.p,jpg.data(),jpg.size()); bo.bytesused=jpg.size();
    if(xioctl(fd,VIDIOC_QBUF,&bc)<0||xioctl(fd,VIDIOC_QBUF,&bo)<0)return 10;
    v4l2_buf_type tc=V4L2_BUF_TYPE_VIDEO_CAPTURE,to=V4L2_BUF_TYPE_VIDEO_OUTPUT; printf("T311_JPEG_DEC_STAGE=streamon\n"); if(xioctl(fd,VIDIOC_STREAMON,&tc)<0||xioctl(fd,VIDIOC_STREAMON,&to)<0)return 11;
    memset(&bc,0,sizeof(bc)); bc.type=V4L2_BUF_TYPE_VIDEO_CAPTURE; bc.memory=V4L2_MEMORY_MMAP; printf("T311_JPEG_DEC_STAGE=wait_capture\n"); if(dq_capture_timeout(fd,&bc,5000)<0){perror("jpeg dec dq cap timeout");return 13;}
    FILE* fo=fopen(outpath,"wb"); if(!fo)return 14; fwrite(mc.p,1,bc.bytesused,fo); fclose(fo);
    printf("T311_JPEG_DEC bytes=%u expected_min=%d\n",bc.bytesused,w*h);
    xioctl(fd,VIDIOC_STREAMOFF,&to); xioctl(fd,VIDIOC_STREAMOFF,&tc); munmap(mo.p,mo.n);munmap(mc.p,mc.n);close(fd);
    bool ok=bc.bytesused>=(unsigned)(w*h); printf("T311_JPEG_DEC_RESULT=%s\n",ok?"PASS":"FAIL"); return ok?0:15;
}
static int ctrlprobe() {
    const uint32_t rear[]={V4L2_CID_BRIGHTNESS,V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE,V4L2_CID_COLORFX,V4L2_CID_SCENE_MODE,V4L2_CID_3A_LOCK,V4L2_CID_AUTO_FOCUS_RANGE,V4L2_CID_AUTO_FOCUS_START,V4L2_CID_AUTO_FOCUS_STOP,V4L2_CID_AUTO_FOCUS_STATUS};
    const uint32_t front[]={V4L2_CID_BRIGHTNESS,V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE,V4L2_CID_COLORFX,V4L2_CID_SCENE_MODE,V4L2_CID_ISO_SENSITIVITY};
    int fail=0;
    for(int cam=0;cam<2;cam++){const char* path=cam?"/dev/video3":"/dev/video1";int fd=open(path,O_RDWR|O_CLOEXEC);if(fd<0){perror(path);fail++;continue;} auto a=cam?front:rear; size_t n=cam?sizeof(front)/4:sizeof(rear)/4; for(size_t i=0;i<n;i++){v4l2_queryctrl q{};q.id=a[i];int r=xioctl(fd,VIDIOC_QUERYCTRL,&q);printf("T311_CTRL node=%s id=0x%x present=%d name=%s\n",path,a[i],r==0,r==0?(char*)q.name:"");if(r)fail++;}close(fd);} printf("T311_CTRLPROBE=%s\n",fail?"FAIL":"PASS");return fail?2:0;
}
int main(int argc,char**argv){setvbuf(stdout,nullptr,_IONBF,0);if(argc<2){fprintf(stderr,"cmdprobe|jpegenc out.jpg [w h]|jpegdec in.jpg out.raw [w h]|ctrlprobe\n");return 1;} std::string c=argv[1]; if(c=="cmdprobe")return cmdprobe(); if(c=="jpegenc"&&argc>=3)return jpegenc(argv[2],argc>=5?atoi(argv[3]):640,argc>=5?atoi(argv[4]):480); if(c=="jpegdec"&&argc>=4)return jpegdec(argv[2],argv[3],argc>=6?atoi(argv[4]):640,argc>=6?atoi(argv[5]):480); if(c=="ctrlprobe")return ctrlprobe(); return 1;}
