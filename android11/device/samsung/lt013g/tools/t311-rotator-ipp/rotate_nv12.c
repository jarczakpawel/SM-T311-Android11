#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <xf86drm.h>
#include <drm_fourcc.h>
#include <drm_mode.h>
#include "exynos_drm_6_12_112_uapi.h"
#define _EXYNOS_DRM_H_
#include "exynos_drmif.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL line=%d errno=%d\n",__LINE__,errno); goto finish; } } while (0)

int main(void) {
    const unsigned sw=640,sh=480,dw=480,dh=640;
    const size_t sy=(size_t)sw*sh,dy=(size_t)dw*dh,bytes=sy*3/2;
    int fd=-1,rc=9;
    struct exynos_device *dev=NULL;
    struct exynos_bo *src=NULL,*dst=NULL;
    uint8_t *s=NULL,*d=NULL;
    struct drm_exynos_ioctl_ipp_get_res res={0};
    uint32_t ids[8]={0},id=UINT32_MAX;
    struct {
        struct drm_exynos_ipp_task_buffer src;
        struct drm_exynos_ipp_task_buffer dst;
        struct drm_exynos_ipp_task_rect sr;
        struct drm_exynos_ipp_task_rect dr;
        struct drm_exynos_ipp_task_transform tr;
    } task={0};
    fd=open("/dev/dri/card0",O_RDWR|O_CLOEXEC);CHECK(fd>=0);
    CHECK(!drmIoctl(fd,DRM_IOCTL_EXYNOS_IPP_GET_RESOURCES,&res));
    CHECK(res.count_ipps && res.count_ipps<=8);
    res.ipp_id_ptr=(uintptr_t)ids;
    CHECK(!drmIoctl(fd,DRM_IOCTL_EXYNOS_IPP_GET_RESOURCES,&res));
    for(unsigned i=0;i<res.count_ipps;i++) {
        struct drm_exynos_ioctl_ipp_get_caps caps={.ipp_id=ids[i]};
        if(!drmIoctl(fd,DRM_IOCTL_EXYNOS_IPP_GET_CAPS,&caps) &&
           (caps.capabilities&DRM_EXYNOS_IPP_CAP_ROTATE)) {id=ids[i];break;}
    }
    CHECK(id!=UINT32_MAX);
    dev=exynos_device_create(fd);CHECK(dev);
    src=exynos_bo_create(dev,bytes,0x18);CHECK(src);
    dst=exynos_bo_create(dev,bytes,0x18);CHECK(dst);
    s=exynos_bo_map(src);d=exynos_bo_map(dst);CHECK(s && d);
    for(unsigned y=0;y<sh;y++)for(unsigned x=0;x<sw;x++)
        s[(size_t)y*sw+x]=(uint8_t)((x*3+y*5+19)&255);
    for(unsigned y=0;y<sh/2;y++)for(unsigned x=0;x<sw/2;x++) {
        s[sy+(size_t)y*sw+x*2]=(uint8_t)((x*11+y*7+33)&255);
        s[sy+(size_t)y*sw+x*2+1]=(uint8_t)((x*13+y*17+91)&255);
    }
    memset(d,0xa5,bytes);
    __asm__ __volatile__("dsb sy" ::: "memory");
    task.src.id=DRM_EXYNOS_IPP_TASK_BUFFER|DRM_EXYNOS_IPP_TASK_TYPE_SOURCE;
    task.src.fourcc=DRM_FORMAT_NV12;task.src.width=sw;task.src.height=sh;
    task.src.gem_id[0]=src->handle;task.src.pitch[0]=sw;
    task.src.gem_id[1]=src->handle;task.src.pitch[1]=sw;task.src.offset[1]=sy;
    task.dst.id=DRM_EXYNOS_IPP_TASK_BUFFER|DRM_EXYNOS_IPP_TASK_TYPE_DESTINATION;
    task.dst.fourcc=DRM_FORMAT_NV12;task.dst.width=dw;task.dst.height=dh;
    task.dst.gem_id[0]=dst->handle;task.dst.pitch[0]=dw;
    task.dst.gem_id[1]=dst->handle;task.dst.pitch[1]=dw;task.dst.offset[1]=dy;
    task.sr.id=DRM_EXYNOS_IPP_TASK_RECTANGLE|DRM_EXYNOS_IPP_TASK_TYPE_SOURCE;
    task.sr.w=sw;task.sr.h=sh;
    task.dr.id=DRM_EXYNOS_IPP_TASK_RECTANGLE|DRM_EXYNOS_IPP_TASK_TYPE_DESTINATION;
    task.dr.w=dw;task.dr.h=dh;
    task.tr.id=DRM_EXYNOS_IPP_TASK_TRANSFORM;task.tr.rotation=DRM_MODE_ROTATE_90;
    struct drm_exynos_ioctl_ipp_commit c={.ipp_id=id,.flags=DRM_EXYNOS_IPP_FLAG_TEST_ONLY,
        .params_size=sizeof(task),.params_ptr=(uintptr_t)&task};
    int check=drmIoctl(fd,DRM_IOCTL_EXYNOS_IPP_COMMIT,&c);
    printf("ROTATOR_NV12_TEST_ONLY_RETURN=%d SOURCE=%ux%u DEST=%ux%u\n",check,sw,sh,dw,dh);
    CHECK(check==0);
    c.flags=DRM_EXYNOS_IPP_FLAG_NONBLOCK|DRM_EXYNOS_IPP_FLAG_EVENT;
    c.user_data=UINT64_C(0x31142012);
    struct timespec begin,end;
    CHECK(!clock_gettime(CLOCK_MONOTONIC,&begin));
    CHECK(!drmIoctl(fd,DRM_IOCTL_EXYNOS_IPP_COMMIT,&c));
    struct pollfd pfd={.fd=fd,.events=POLLIN};
    CHECK(poll(&pfd,1,1200)>0 && (pfd.revents&POLLIN));
    struct drm_exynos_ipp_event ev={0};
    CHECK(read(fd,&ev,sizeof(ev))==(ssize_t)sizeof(ev));
    CHECK(!clock_gettime(CLOCK_MONOTONIC,&end));
    CHECK(ev.base.type==DRM_EXYNOS_IPP_EVENT && ev.user_data==c.user_data);
    __asm__ __volatile__("dsb sy" ::: "memory");
    unsigned bad_y=0,bad_u=0,bad_v=0;
    for(unsigned y=0;y<dh;y++)for(unsigned x=0;x<dw;x++) {
        const uint8_t expected=s[(size_t)(sh-1-x)*sw+y];
        if(d[(size_t)y*dw+x]!=expected) {
            if(bad_y<3)fprintf(stderr,"Y x=%u y=%u got=%u exp=%u\n",x,y,d[(size_t)y*dw+x],expected);
            bad_y++;
        }
    }
    for(unsigned y=0;y<dh/2;y++)for(unsigned x=0;x<dw/2;x++) {
        const size_t sp=sy+(size_t)(sh/2-1-x)*sw+y*2;
        const size_t dp=dy+(size_t)y*dw+x*2;
        if(d[dp]!=s[sp]) {
            if(bad_u<3)fprintf(stderr,"U x=%u y=%u got=%u exp=%u\n",x,y,d[dp],s[sp]);
            bad_u++;
        }
        if(d[dp+1]!=s[sp+1]) {
            if(bad_v<3)fprintf(stderr,"V x=%u y=%u got=%u exp=%u\n",x,y,d[dp+1],s[sp+1]);
            bad_v++;
        }
    }
    const int64_t elapsed=(int64_t)(end.tv_sec-begin.tv_sec)*1000000000LL+end.tv_nsec-begin.tv_nsec;
    printf("ROTATOR_NV12_REAL_DMA_Y=%zu UV_PAIRS=%zu Y_ERRORS=%u U_ERRORS=%u V_ERRORS=%u WALL_US=%.2f EVENT_SEQ=%u\n",
        dy,dy/4,bad_y,bad_u,bad_v,(double)elapsed/1000.,ev.sequence);
    CHECK(!bad_y && !bad_u && !bad_v);
    printf("ROTATOR_NV12_HARDWARE=PASS\n");
    rc=0;
finish:
    if(dst)exynos_bo_destroy(dst);
    if(src)exynos_bo_destroy(src);
    if(dev)exynos_device_destroy(dev);
    if(fd>=0)close(fd);
    return rc;
}
