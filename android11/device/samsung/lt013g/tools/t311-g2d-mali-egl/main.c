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
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <system/window.h>
#include <hardware/gralloc.h>
#include <cutils/native_handle.h>

#include <drm.h>
#include "drv.h"
#include "exynos_drm.h"
#include "exynos_fimg2d.h"
#include "g2d_bridge.h"


/* Exact cros_gralloc_handle layout used by installed minigbm gralloc0,
 * as published in external/minigbm/cros_gralloc/cros_gralloc_handle.h.
 * This offline handle is private to the test, never passed to HWC.
 */
struct t311_test_cros_handle {
    native_handle_t base;
    int32_t fds[5];
    uint32_t strides[4];
    uint32_t offsets[4];
    uint32_t sizes[4];
    uint32_t id,width,height,format;
    uint64_t format_modifier,use_flags;
    uint32_t magic,pixel_stride;
    int32_t droid_format,usage;
    uint32_t num_planes;
    uint64_t reserved_region_size,total_size;
    uint32_t name_offset;
} __attribute__((packed));
static void test_native_inc_ref(struct android_native_base_t *base) {(void)base;}
static void test_native_dec_ref(struct android_native_base_t *base) {(void)base;}
/* The only draw uses an EGL pbuffer of 2x2 pixels, never a system layer.
 * The sampled dma-buf is a different private G2D-written minigbm buffer.
 */
static int gpu_sample_g2d_dma_buf(int prime_fd,unsigned w,unsigned h,unsigned stride) {
    EGLDisplay d=EGL_NO_DISPLAY;
    EGLContext c=EGL_NO_CONTEXT;
    EGLSurface surf=EGL_NO_SURFACE;
    EGLImageKHR image=EGL_NO_IMAGE_KHR;
    GLuint tex=0,vs=0,fs=0,prog=0,buf=0;
    struct t311_test_cros_handle *ch=NULL;
    int ret=1;
    d=eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if(d==EGL_NO_DISPLAY || !eglInitialize(d,NULL,NULL)) {fprintf(stderr,"GLES_EGL_DISPLAY_INIT_FAIL=0x%x\n",eglGetError());goto done;}
    const char *ext=eglQueryString(d,EGL_EXTENSIONS);
    printf("EGL_VENDOR=%s\n",eglQueryString(d,EGL_VENDOR));
    printf("EGL_HAS_DMA_BUF_IMPORT=%s\n",ext && strstr(ext,"EGL_EXT_image_dma_buf_import")?"YES":"NO");
    printf("EGL_HAS_ANDROID_NATIVE_BUFFER=%s\n",ext && strstr(ext,"EGL_ANDROID_image_native_buffer")?"YES":"NO");
    if(!ext || !strstr(ext,"EGL_ANDROID_image_native_buffer")){
        fprintf(stderr,"MALI_GLES_ANDROID_NATIVE_BUFFER_IMPORT=UNSUPPORTED_AT_EGL_API\n");ret=7;goto done;
    }
    EGLint attrib[]={EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,
          EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_NONE};
    EGLConfig cfg;EGLint count=0;
    if(!eglChooseConfig(d,attrib,&cfg,1,&count)||!count){fprintf(stderr,"EGL_NO_PBUFFER_ES2_CONFIG\n");goto done;}
    EGLint surface_attrs[]={EGL_WIDTH,2,EGL_HEIGHT,2,EGL_NONE};
    surf=eglCreatePbufferSurface(d,cfg,surface_attrs);
    EGLint ctx_attrs[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
    c=eglCreateContext(d,cfg,EGL_NO_CONTEXT,ctx_attrs);
    if(surf==EGL_NO_SURFACE||c==EGL_NO_CONTEXT||!eglMakeCurrent(d,surf,surf,c)){
        fprintf(stderr,"EGL_OFFSCREEN_CONTEXT_FAIL=0x%x\n",eglGetError());goto done;
    }
    printf("GLES_RENDERER=%s\n",glGetString(GL_RENDERER));
    PFNEGLCREATEIMAGEKHRPROC create=(PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC target=(PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
    if(!create||!target){fprintf(stderr,"EGL_DMA_BUF_IMPORT_FUNCTION_MISSING\n");goto done;}
    /* Android's platform EGL exports mandatory EGL_ANDROID_image_native_buffer,
     * while it can intentionally hide EGL_EXT_image_dma_buf_import from apps.
     * Build an *isolated* ANativeWindowBuffer with one real PRIME fd and a
     * lifetime that extends until eglDestroyImage. No producer or HWC buffer.
     */
    ch=calloc(1,sizeof(*ch));
    if(!ch){fprintf(stderr,"CROS_HANDLE_ALLOC_FAIL\n");goto done;}
    ch->base.version=sizeof(native_handle_t);
    ch->base.numFds=1;
    ch->base.numInts=(int)((sizeof(*ch)-sizeof(native_handle_t))/sizeof(int))-1;
    ch->fds[0]=dup(prime_fd);
    if(ch->fds[0]<0){fprintf(stderr,"DUP_BUFFER_FD_FAIL\n");goto done;}
    ch->strides[0]=stride;
    ch->sizes[0]=stride*h;
    ch->width=w;ch->height=h;
    ch->format=DRM_FORMAT_ARGB8888;
    ch->format_modifier=0;
    ch->magic=0xABCDDCBA;
    ch->pixel_stride=stride/4;
    ch->droid_format=HAL_PIXEL_FORMAT_BGRA_8888;
    ch->usage=GRALLOC_USAGE_HW_TEXTURE;
    ch->num_planes=1;
    ch->total_size=(uint64_t)stride*h;
    struct ANativeWindowBuffer nativebuf;
    memset(&nativebuf,0,sizeof(nativebuf));
    nativebuf.common.magic=ANDROID_NATIVE_BUFFER_MAGIC;
    nativebuf.common.version=sizeof(nativebuf);
    nativebuf.common.incRef=test_native_inc_ref;
    nativebuf.common.decRef=test_native_dec_ref;
    nativebuf.width=(int)w;
    nativebuf.height=(int)h;
    nativebuf.stride=(int)(stride/4);
    nativebuf.format=HAL_PIXEL_FORMAT_BGRA_8888;
    nativebuf.usage=GRALLOC_USAGE_HW_TEXTURE;
    nativebuf.handle=(const native_handle_t *)(const void *)ch;
    printf("GLES_NATIVE_GRALLOC0_HANDLE_MAGIC=0x%x FDS=%d STRIDE=%u\n",ch->magic,ch->base.numFds,ch->strides[0]);
    EGLint img_attr[]={EGL_NONE};
    image=create(d,EGL_NO_CONTEXT,EGL_NATIVE_BUFFER_ANDROID,(EGLClientBuffer)&nativebuf,img_attr);
    if(image==EGL_NO_IMAGE_KHR){fprintf(stderr,"EGL_IMPORT_G2D_WRITTEN_PRIME_FAILURE=0x%x\n",eglGetError());goto done;}
    glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    target(GL_TEXTURE_2D,(GLeglImageOES)image);
    GLenum err=glGetError();
    if(err!=GL_NO_ERROR){fprintf(stderr,"GLES_IMPORT_DMA_BUF_TEXTURE_FAILURE=0x%x\n",err);goto done;}
    const char *vsrc="attribute vec2 a; void main(){gl_Position=vec4(a,0.0,1.0);}";
    const char *fsrc="precision mediump float; uniform sampler2D t; void main(){gl_FragColor=texture2D(t,vec2(0.5,0.5));}";
    vs=glCreateShader(GL_VERTEX_SHADER);glShaderSource(vs,1,&vsrc,NULL);glCompileShader(vs);
    fs=glCreateShader(GL_FRAGMENT_SHADER);glShaderSource(fs,1,&fsrc,NULL);glCompileShader(fs);
    GLint ok=0;glGetShaderiv(vs,GL_COMPILE_STATUS,&ok);
    if(!ok){fprintf(stderr,"GLES_VERTEX_SHADER_FAIL\n");goto done;}
    glGetShaderiv(fs,GL_COMPILE_STATUS,&ok);
    if(!ok){fprintf(stderr,"GLES_FRAGMENT_SHADER_FAIL\n");goto done;}
    prog=glCreateProgram();glAttachShader(prog,vs);glAttachShader(prog,fs);
    glBindAttribLocation(prog,0,"a");glLinkProgram(prog);
    glGetProgramiv(prog,GL_LINK_STATUS,&ok);
    if(!ok){fprintf(stderr,"GLES_PROGRAM_LINK_FAIL\n");goto done;}
    const GLfloat vertices[]={-1.f,-1.f,1.f,-1.f,-1.f,1.f,1.f,1.f};
    glGenBuffers(1,&buf);glBindBuffer(GL_ARRAY_BUFFER,buf);
    glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
    glUseProgram(prog);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,tex);
    glUniform1i(glGetUniformLocation(prog,"t"),0);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,0);glEnableVertexAttribArray(0);
    glViewport(0,0,2,2);glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    unsigned char rgba[4]={0};glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
    glFinish();err=glGetError();
    printf("G2D_TO_MALI_GLES_RGBA_SAMPLE=0x%02x%02x%02x%02x GL_ERR=0x%x\n",rgba[0],rgba[1],rgba[2],rgba[3],err);
    /* g2d solid color ARGB 0xff2d7c91 -> GL RGBA 2d,7c,91,ff */
    ret=(err==GL_NO_ERROR && rgba[0]==0x2d && rgba[1]==0x7c && rgba[2]==0x91 && rgba[3]==0xff)?0:9;
    printf("G2D_TO_MALI_REAL_GLES_TEXTURE_SAMPLE=%s\n",ret==0?"PASS":"FAIL");
done:
    if(buf)glDeleteBuffers(1,&buf);
    if(prog)glDeleteProgram(prog);
    if(vs)glDeleteShader(vs);
    if(fs)glDeleteShader(fs);
    if(tex)glDeleteTextures(1,&tex);
    if(image!=EGL_NO_IMAGE_KHR){
        PFNEGLDESTROYIMAGEKHRPROC destroy=(PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
        if(destroy)destroy(d,image);
    }
    if(ch){if(ch->fds[0]>=0)close(ch->fds[0]);free(ch);}
    if(d!=EGL_NO_DISPLAY){eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
        if(c!=EGL_NO_CONTEXT)eglDestroyContext(d,c);
        if(surf!=EGL_NO_SURFACE)eglDestroySurface(d,surf);
        eglTerminate(d);
    }
    return ret;
}

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
    REQUIRE(gpu_sample_g2d_dma_buf(prime,w,h,stride)==0,"mali_gles_render_samples_g2d_dma_buf");
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
