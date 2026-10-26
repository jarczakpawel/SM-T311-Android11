/* SM-T311: source FROM REAL Exynos minigbm backend, not libdrm BO wrapper.
 * PRIME-export -> SECOND Exynos DRM FD import -> synchronous hardware G2D
 * fill; independently PRIME-import into Mali DRM. No compositor surfaces,
 * no display or boot writes, no render commands to Mali.
 */
#define _GNU_SOURCE 1
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xf86drm.h>
#include <drm_fourcc.h>
#include <drm.h>
#include "drv.h"
#include "exynos_drm.h"
#include "exynos_fimg2d.h"
#include "g2d_bridge.h"

#define REQUIRE(x,label) do { if(!(x)) { fprintf(stderr,"FAIL=%s errno=%d\n",label,errno); goto end; } } while(0)
static int gem_close(int fd, uint32_t handle) {
    struct drm_gem_close req={.handle=handle};
    return drmIoctl(fd,DRM_IOCTL_GEM_CLOSE,&req);
}
int main(int argc,char **argv) {
    unsigned w=256,h=256;
    int f0=-1,f1=-1,fm=-1,prime=-1,prime_src=-1,ret=6,real_fence=-1;
    uint32_t real_syncobj=0;
    struct driver *drv=NULL;
    struct bo *bo=NULL,*srcbo=NULL;
    struct mapping *map=NULL,*srcmap=NULL;
    uint32_t *pix=NULL,*srcpix=NULL,h1=0,hm=0,h1_src=0;
    struct g2d_context *g2d=NULL;
    uint32_t stride=0,format=DRM_FORMAT_ARGB8888;
    if (argc!=3 || sscanf(argv[1],"%u",&w)!=1 || sscanf(argv[2],"%u",&h)!=1 || w<32 || w>640 || h<32 || h>800 || w%16 || h%16) {
        fprintf(stderr,"USAGE: %s width(32..640, aligned16) height(32..800, aligned16)\n",argv[0]);return 2;
    }
    f0=open("/dev/dri/card0",O_RDWR|O_CLOEXEC);
    f1=open("/dev/dri/card0",O_RDWR|O_CLOEXEC);
    fm=open("/dev/dri/renderD129",O_RDWR|O_CLOEXEC);
    REQUIRE(f0>=0 && f1>=0 && fm>=0,"open_3_drm_fds");
    drv=drv_create(f0); REQUIRE(drv,"minigbm_driver_create");
    REQUIRE(!strcmp(drv_get_name(drv),"exynos"),"minigbm_is_exynos");
    /* BO_USE_CAMERA_WRITE requests the already deployed coherent+kmap=0x18
     * on the actual minigbm Exynos ARGB8888 path; not globally changing BOs.
     */
    bo=drv_bo_create(drv,w,h,format,BO_USE_CAMERA_WRITE);
    REQUIRE(bo,"real_minigbm_argb8888_create");
    REQUIRE(drv_bo_get_num_planes(bo)==1,"single_plane");
    stride=drv_bo_get_plane_stride(bo,0);
    const uint32_t original_handle=drv_bo_get_plane_handle(bo,0).u32;
    struct drm_exynos_gem_info gi={.handle=original_handle};
    REQUIRE(drmIoctl(f0,DRM_IOCTL_EXYNOS_GEM_GET,&gi)==0,"get_original_gem_info");
    printf("REAL_MINIGBM_EXYNOS_BO=%ux%u stride=%u flags=0x%x size=%llu\n",w,h,stride,gi.flags,(unsigned long long)gi.size);
    REQUIRE((gi.flags&0x18)==0x18,"trusted_cpu_dma_mapping_0x18");
    REQUIRE(stride>=w*4 && stride%4==0 && gi.size>=(uint64_t)stride*h,"linear_argb_stride_bounds");
    srcbo=drv_bo_create(drv,w,h,format,BO_USE_CAMERA_WRITE);
    REQUIRE(srcbo && drv_bo_get_num_planes(srcbo)==1 && drv_bo_get_plane_stride(srcbo,0)==stride,"matching_real_minigbm_source");
    const uint32_t source_handle=drv_bo_get_plane_handle(srcbo,0).u32;
    struct drm_exynos_gem_info gis={.handle=source_handle};
    REQUIRE(drmIoctl(f0,DRM_IOCTL_EXYNOS_GEM_GET,&gis)==0 && (gis.flags&0x18)==0x18,"source_gem_coherent_kmap");
    const struct rectangle rect={.x=0,.y=0,.width=w,.height=h};
    pix=drv_bo_map(bo,&rect,BO_MAP_READ_WRITE,&map,0);
    REQUIRE(pix && pix!=MAP_FAILED && map,"gralloc_bo_map");
    srcpix=drv_bo_map(srcbo,&rect,BO_MAP_READ_WRITE,&srcmap,0);
    REQUIRE(srcpix && srcpix!=MAP_FAILED && srcmap,"source_gralloc_bo_map");
    memset(pix,0,(size_t)stride*h);
    const unsigned pitch=stride/4;
    for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)
        srcpix[(size_t)y*pitch+x]=UINT32_C(0xff000000)|
            (((uint32_t)(y*pitch+x)*UINT32_C(2654435761))&UINT32_C(0x00ffffff));
    __asm__ __volatile__("dsb sy":::"memory");
    prime=drv_bo_get_plane_fd(bo,0);
    REQUIRE(prime>=0,"real_gralloc_prime_export");
    prime_src=drv_bo_get_plane_fd(srcbo,0);
    REQUIRE(prime_src>=0,"real_gralloc_source_prime_export");
    REQUIRE(drmPrimeFDToHandle(f1,prime_src,&h1_src)==0 && h1_src,"source_prime_to_independent_exynos_fd");
    REQUIRE(drmPrimeFDToHandle(f1,prime,&h1)==0 && h1,"prime_to_independent_exynos_fd");
    printf("REAL_MINIGBM_PRIME_INTO_G2D_CARD0=PASS handle=%u\n",h1);
    /* Crucial: also show Mali can PRIME import SAME dma-buf; do not issue GPU render. */
    if(drmPrimeFDToHandle(fm,prime,&hm)==0 && hm)
        printf("REAL_MINIGBM_PRIME_INTO_MALI_RENDERD129=PASS handle=%u\n",hm);
    else { printf("REAL_MINIGBM_PRIME_INTO_MALI_RENDERD129=FAIL errno=%d\n",errno);goto end; }
    g2d=g2d_init(f1);
    REQUIRE(g2d,"g2d_context_secondary_fd");
    struct g2d_image img={0};
    img.width=w;img.height=h;img.stride=stride;
    img.color_mode=G2D_COLOR_FMT_ARGB8888|G2D_ORDER_AXRGB;
    img.select_mode=G2D_SELECT_MODE_NORMAL;
    img.buf_type=G2D_IMGBUF_GEM;
    img.bo[0]=h1;
    img.color=UINT32_C(0xff2d7c91);
    REQUIRE(g2d_solid_fill(g2d,&img,0,0,w,h)==0,"g2d_fill_imported_minigbm_bo_enqueue");
    REQUIRE(g2d_exec(g2d)==0,"g2d_fill_imported_minigbm_bo_sync");
    unsigned bad=0;uint32_t first=0;
    for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){
       const uint32_t got=pix[(size_t)y*pitch+x];
       if(!x&&!y)first=got;
       if(got!=UINT32_C(0xff2d7c91)){if(bad<4)fprintf(stderr,"PIXEL_MISMATCH x=%u y=%u got=0x%08x\n",x,y,got);bad++;}
    }
    printf("REAL_MINIGBM_DMA_G2D_PIXELS=%u MISMATCHES=%u FIRST=0x%08x\n",w*h,bad,first);
    REQUIRE(bad==0,"true_minigbm_g2d_zero_copy_pixel_verify");
    /* A real source-to-destination 2D DMA copy; both source and destination
     * originate from the actual minigbm allocator and are PRIME imported
     * by an independent G2D DRM FD. No CPU copy between GEM allocations.
     */
    struct g2d_image srcimg=img;
    srcimg.bo[0]=h1_src;
    struct t311_g2d_timings timings;
    /* Bad FD MUST fail *before* queued G2D memory writes. Source pattern
     * remains valid even if no actual kernel sync_file is available here.
     */
    int fake=open("/dev/null",O_RDONLY|O_CLOEXEC);
    REQUIRE(fake>=0,"open_invalid_fence_fd");
    int negative=t311_g2d_copy_sync(g2d,&srcimg,&img,w,h,fake,-1,16,&timings);
    int negative_dst=t311_g2d_copy_sync(g2d,&srcimg,&img,w,h,-1,fake,16,&timings);
    close(fake);
    printf("G2D_BRIDGE_REJECT_INVALID_PRODUCER_FENCE=%s errno=%d\n",negative==-EINVAL?"PASS":"FAIL",negative);
    printf("G2D_BRIDGE_REJECT_INVALID_DESTINATION_FENCE=%s errno=%d\n",negative_dst==-EINVAL?"PASS":"FAIL",negative_dst);
    REQUIRE(negative==-EINVAL && negative_dst==-EINVAL,"reject_nonsyncfile_fences_before_dma");
    unsigned unchanged=0;
    for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)
        unchanged+=(pix[(size_t)y*pitch+x]!=UINT32_C(0xff2d7c91));
    printf("G2D_BRIDGE_INVALID_FENCE_DEST_BUFFER_UNCHANGED=%s bad=%u\n",unchanged==0?"PASS":"FAIL",unchanged);
    REQUIRE(unchanged==0,"error_paths_must_not_submit_dma");
    uint64_t syncobj_support=0;
    if(drmGetCap(fm,DRM_CAP_SYNCOBJ,&syncobj_support)==0 && syncobj_support) {
        if(drmSyncobjCreate(fm,DRM_SYNCOBJ_CREATE_SIGNALED,&real_syncobj)==0 && real_syncobj &&
           drmSyncobjExportSyncFile(fm,real_syncobj,&real_fence)==0 && real_fence>=0) {
            int fenced=t311_g2d_copy_sync(g2d,&srcimg,&img,w,h,real_fence,real_fence,16,&timings);
            printf("G2D_BRIDGE_REAL_SIGNALLED_SYNC_FILE_PRODUCER_AND_DEST=%s errno=%d\n",fenced==0?"PASS":"FAIL",fenced);
            REQUIRE(fenced==0,"real_signaled_sync_file_validation_and_g2d_copy");
        } else {
            printf("G2D_BRIDGE_REAL_SYNC_FILE_EXPORT=NOT_AVAILABLE errno=%d\n",errno);
        }
    } else {
        printf("G2D_BRIDGE_DRM_SYNCOBJ_CAP=UNSUPPORTED_ON_MALI\n");
    }
    REQUIRE(t311_g2d_copy_sync(g2d,&srcimg,&img,w,h,-1,-1,16,&timings)==0,"g2d_copy_true_minigbm_prime_buffers_sync");
    printf("G2D_BRIDGE_UNFENCED_SOURCE_TO_DEST_WAIT_US=%.2f EXEC_US=%.2f\n",
           (double)timings.fence_wait_ns/1000.0,(double)timings.g2d_exec_ns/1000.0);
    unsigned copybad=0,sourcebad=0;
    for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){
       const uint32_t expected=UINT32_C(0xff000000)|
          (((uint32_t)(y*pitch+x)*UINT32_C(2654435761))&UINT32_C(0x00ffffff));
       const uint32_t found=pix[(size_t)y*pitch+x];
       const uint32_t sourced=srcpix[(size_t)y*pitch+x];
       if(found!=expected){if(copybad<4)fprintf(stderr,"COPY_MISMATCH x=%u y=%u got=%08x expected=%08x\n",x,y,found,expected);copybad++;}
       if(sourced!=expected)sourcebad++;
    }
    printf("REAL_MINIGBM_TO_MINIGBM_PRIME_G2D_COPY_PIXELS=%u MISMATCHES=%u SOURCE_ERRORS=%u\n",w*h,copybad,sourcebad);
    REQUIRE(copybad==0 && sourcebad==0,"true_minigbm_to_minigbm_zero_copy");
    printf("MINIGBM_PRIME_EXYNOS_G2D_MALI_INTEROP=PASS\n");
    ret=0;
end:
    if(real_fence>=0)close(real_fence);
    if(real_syncobj)drmSyncobjDestroy(fm,real_syncobj);
    if(g2d)g2d_fini(g2d);
    if(hm)gem_close(fm,hm);
    if(h1)gem_close(f1,h1);
    if(h1_src)gem_close(f1,h1_src);
    if(prime>=0)close(prime);
    if(prime_src>=0)close(prime_src);
    if(map&&bo)drv_bo_unmap(bo,map);
    if(srcmap&&srcbo)drv_bo_unmap(srcbo,srcmap);
    if(srcbo)drv_bo_destroy(srcbo);
    if(bo)drv_bo_destroy(bo);
    if(drv)drv_destroy(drv);
    if(fm>=0)close(fm);
    if(f1>=0)close(f1);
    if(f0>=0)close(f0);
    return ret;
}
