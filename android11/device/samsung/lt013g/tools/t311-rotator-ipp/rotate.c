#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xf86drm.h>
#include <drm_fourcc.h>
#include <drm_mode.h>
#include "exynos_drm_6_12_112_uapi.h"
#define _EXYNOS_DRM_H_
#include "exynos_drmif.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAILED line=%d errno=%d\n", __LINE__, errno); goto finish; } } while (0)

int main(int argc, char **argv) {
    const unsigned sw=64,sh=128,dw=128,dh=64;
    int fd=-1,rc=9;
    struct exynos_device *dev=NULL;
    struct exynos_bo *src=NULL,*dst=NULL;
    uint32_t *s=NULL,*d=NULL;
    struct drm_exynos_ioctl_ipp_get_res res={0};
    uint32_t ids[8]={0};
    uint32_t id=UINT32_MAX;
    struct {
        struct drm_exynos_ipp_task_buffer src;
        struct drm_exynos_ipp_task_buffer dst;
        struct drm_exynos_ipp_task_rect sr;
        struct drm_exynos_ipp_task_rect dr;
        struct drm_exynos_ipp_task_transform tr;
    } task={0};
    if (argc!=2 || (strcmp(argv[1],"check") && strcmp(argv[1],"run"))) return 2;
    fd=open("/dev/dri/card0",O_RDWR|O_CLOEXEC);CHECK(fd>=0);
    CHECK(drmIoctl(fd,DRM_IOCTL_EXYNOS_IPP_GET_RESOURCES,&res)==0);
    CHECK(res.count_ipps && res.count_ipps<=8);
    res.ipp_id_ptr=(uintptr_t)ids;
    CHECK(drmIoctl(fd,DRM_IOCTL_EXYNOS_IPP_GET_RESOURCES,&res)==0);
    for(unsigned i=0;i<res.count_ipps;i++) {
        struct drm_exynos_ioctl_ipp_get_caps caps={.ipp_id=ids[i]};
        if(!drmIoctl(fd,DRM_IOCTL_EXYNOS_IPP_GET_CAPS,&caps) &&
           (caps.capabilities & DRM_EXYNOS_IPP_CAP_ROTATE)) {id=ids[i];break;}
    }
    CHECK(id!=UINT32_MAX);
    dev=exynos_device_create(fd);CHECK(dev);
    src=exynos_bo_create(dev,(size_t)sw*sh*4,0x18);CHECK(src);
    dst=exynos_bo_create(dev,(size_t)dw*dh*4,0x18);CHECK(dst);
    s=exynos_bo_map(src);d=exynos_bo_map(dst);CHECK(s && d);
    for(unsigned y=0;y<sh;y++)for(unsigned x=0;x<sw;x++)
        s[(size_t)y*sw+x]=UINT32_C(0xff000000)|((y*191+x*317)&UINT32_C(0xffffff));
    memset(d,0,(size_t)dw*dh*4);
    __asm__ __volatile__("dsb sy" ::: "memory");
    task.src.id=DRM_EXYNOS_IPP_TASK_BUFFER|DRM_EXYNOS_IPP_TASK_TYPE_SOURCE;
    task.src.fourcc=DRM_FORMAT_XRGB8888;task.src.width=sw;task.src.height=sh;
    task.src.pitch[0]=sw*4;task.src.gem_id[0]=src->handle;
    task.dst.id=DRM_EXYNOS_IPP_TASK_BUFFER|DRM_EXYNOS_IPP_TASK_TYPE_DESTINATION;
    task.dst.fourcc=DRM_FORMAT_XRGB8888;task.dst.width=dw;task.dst.height=dh;
    task.dst.pitch[0]=dw*4;task.dst.gem_id[0]=dst->handle;
    task.sr.id=DRM_EXYNOS_IPP_TASK_RECTANGLE|DRM_EXYNOS_IPP_TASK_TYPE_SOURCE;
    task.sr.w=sw;task.sr.h=sh;
    task.dr.id=DRM_EXYNOS_IPP_TASK_RECTANGLE|DRM_EXYNOS_IPP_TASK_TYPE_DESTINATION;
    task.dr.w=dw;task.dr.h=dh;
    task.tr.id=DRM_EXYNOS_IPP_TASK_TRANSFORM;task.tr.rotation=DRM_MODE_ROTATE_90;
    struct drm_exynos_ioctl_ipp_commit c={.ipp_id=id,.flags=DRM_EXYNOS_IPP_FLAG_TEST_ONLY,
        .params_size=sizeof(task),.params_ptr=(uintptr_t)&task};
    int ret=drmIoctl(fd,DRM_IOCTL_EXYNOS_IPP_COMMIT,&c);
    printf("ROTATOR_IPP_ID=%u TEST_ONLY=%d errno=%d source=%ux%u dst=%ux%u\n",id,ret,errno,sw,sh,dw,dh);
    CHECK(ret==0);
    if(!strcmp(argv[1],"check")){rc=0;goto finish;}
    c.flags=DRM_EXYNOS_IPP_FLAG_NONBLOCK|DRM_EXYNOS_IPP_FLAG_EVENT;
    c.user_data=UINT64_C(0x3114212);
    CHECK(drmIoctl(fd,DRM_IOCTL_EXYNOS_IPP_COMMIT,&c)==0);
    struct pollfd pfd={.fd=fd,.events=POLLIN};
    CHECK(poll(&pfd,1,1200)>0 && (pfd.revents&POLLIN));
    struct drm_exynos_ipp_event ev={0};
    CHECK(read(fd,&ev,sizeof(ev))==(ssize_t)sizeof(ev));
    CHECK(ev.base.type==DRM_EXYNOS_IPP_EVENT && ev.user_data==c.user_data);
    __asm__ __volatile__("dsb sy" ::: "memory");
    unsigned cw=0,ccw=0;
    for(unsigned y=0;y<dh;y++)for(unsigned x=0;x<dw;x++){
       uint32_t v=d[(size_t)y*dw+x];
       cw+=(v!=s[(size_t)(sh-1-x)*sw+y]);
       ccw+=(v!=s[(size_t)x*sw+(sw-1-y)]);
    }
    printf("ROTATOR_REAL_DMA_90_PIXELS=%u CLOCKWISE_MISMATCH=%u COUNTERCLOCKWISE_MISMATCH=%u EVENT_SEQUENCE=%u\n",dw*dh,cw,ccw,ev.sequence);
    CHECK(cw==0 || ccw==0);
    rc=0;
finish:
    if(dst)exynos_bo_destroy(dst);
    if(src)exynos_bo_destroy(src);
    if(dev)exynos_device_destroy(dev);
    if(fd>=0)close(fd);
    return rc;
}
