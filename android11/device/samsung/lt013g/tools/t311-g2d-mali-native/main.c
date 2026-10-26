/* PHYSICAL SM-T311 actual gralloc0 Android native buffer, G2D private DMA,
 * Lima GLES2 offscreen sampling. No screen layer, no framebuffer writes.
 */
#define _GNU_SOURCE 1
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <inttypes.h>
#include <hardware/hardware.h>
#include <hardware/gralloc.h>
#include <system/window.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <xf86drm.h>
#include <drm.h>
#include <exynos_drm.h>
#include <exynos_fimg2d.h>

static void buffer_inc(android_native_base_t *b) {(void)b;}
static void buffer_dec(android_native_base_t *b) {(void)b;}
static int gpu_sample_g2d_native_buffer(ANativeWindowBuffer *nb) {
    EGLDisplay d=EGL_NO_DISPLAY;
    EGLContext c=EGL_NO_CONTEXT;
    EGLSurface surf=EGL_NO_SURFACE;
    EGLImageKHR image=EGL_NO_IMAGE_KHR;
    GLuint tex=0,vs=0,fs=0,prog=0,buf=0;
    int ret=1;
    d=eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if(d==EGL_NO_DISPLAY || !eglInitialize(d,NULL,NULL)) {fprintf(stderr,"GLES_EGL_DISPLAY_INIT_FAIL=0x%x\n",eglGetError());goto done;}
    const char *ext=eglQueryString(d,EGL_EXTENSIONS);
    printf("EGL_VENDOR=%s\n",eglQueryString(d,EGL_VENDOR));
    printf("EGL_HAS_NATIVE_BUFFER_IMAGE=%s\n",ext && strstr(ext,"EGL_ANDROID_image_native_buffer")?"YES":"NO");
    if(!ext || !strstr(ext,"EGL_ANDROID_image_native_buffer")){
        fprintf(stderr,"EGL_NATIVE_BUFFER_ANDROID_IMPORT=UNSUPPORTED\n");ret=7;goto done;
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
    image=create(d,EGL_NO_CONTEXT,EGL_NATIVE_BUFFER_ANDROID,(EGLClientBuffer)nb,NULL);
    if(image==EGL_NO_IMAGE_KHR){fprintf(stderr,"EGL_IMPORT_G2D_WRITTEN_ANDROID_NATIVE_BUFFER_FAILURE=0x%x\n",eglGetError());goto done;}
    glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    target(GL_TEXTURE_2D,(GLeglImageOES)image);
    GLenum err=glGetError();
    if(err!=GL_NO_ERROR){fprintf(stderr,"GLES_IMPORT_NATIVE_BUFFER_TEXTURE_FAILURE=0x%x\n",err);goto done;}
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
    printf("G2D_NATIVE_GRALLOC_TO_MALI_GLES_TEXTURE_SAMPLE=%s\n",ret==0?"PASS":"FAIL");
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
    if(d!=EGL_NO_DISPLAY){eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
        if(c!=EGL_NO_CONTEXT)eglDestroyContext(d,c);
        if(surf!=EGL_NO_SURFACE)eglDestroySurface(d,surf);
        eglTerminate(d);
    }
    return ret;
}


#define CHECK(x,name) do {if(!(x)){fprintf(stderr,"FAILED_STAGE=%s\n",name);goto cleanup;}}while(0)
int main(void) {
    const unsigned w=256,h=256;
    const int usage=GRALLOC_USAGE_HW_CAMERA_WRITE|GRALLOC_USAGE_HW_TEXTURE;
    const struct hw_module_t *hw=NULL;
    alloc_device_t *alloc=NULL;
    buffer_handle_t handle=NULL;
    int stride=0,fd=-1;
    struct g2d_context *g2d=NULL;
    uint32_t gem=0;
    int rc=8;
    CHECK(hw_get_module(GRALLOC_HARDWARE_MODULE_ID,&hw)==0 && hw,"hw_get_module_real_gralloc");
    printf("GRALLOC_HAL_NAME=%s\n",hw->name);
    CHECK(gralloc_open(hw,&alloc)==0 && alloc,"gralloc_open");
    CHECK(alloc->alloc(alloc,w,h,HAL_PIXEL_FORMAT_BGRA_8888,usage,&handle,&stride)==0 && handle,"gralloc_alloc_256_BGRA8888_CAMERA_TEXTURE");
    printf("REAL_ANDROID_GRALLOC_BUFFER=%ux%u stride_px=%d pixel_format=%d fds=%d\n",w,h,stride,HAL_PIXEL_FORMAT_BGRA_8888,handle->numFds);
    CHECK(handle->numFds==1 && handle->data[0]>=0 && stride>=(int)w,"gralloc_native_handle_linear_plane");
    fd=open("/dev/dri/card0",O_RDWR|O_CLOEXEC);CHECK(fd>=0,"g2d_drm_open");
    CHECK(drmPrimeFDToHandle(fd,handle->data[0],&gem)==0 && gem,"gralloc_handle_import_to_exynos_g2d");
    struct drm_exynos_gem_info info={.handle=gem};
    CHECK(drmIoctl(fd,DRM_IOCTL_EXYNOS_GEM_GET,&info)==0,"native_gralloc_exynos_gem_info");
    printf("REAL_ANDROID_GRALLOC_EXYNOS_GEM_FLAGS=0x%x size=%llu\n",info.flags,(unsigned long long)info.size);
    CHECK((info.flags&0x18)==0x18 && info.size>=stride*h*4U,"coherent_kernel_mapping_and_bounds");
    g2d=g2d_init(fd);CHECK(g2d,"g2d_init");
    struct g2d_image img={0};
    img.width=w;img.height=h;img.stride=(unsigned)stride*4;
    img.color_mode=G2D_COLOR_FMT_ARGB8888|G2D_ORDER_AXRGB;
    img.select_mode=G2D_SELECT_MODE_NORMAL;
    img.buf_type=G2D_IMGBUF_GEM;
    img.bo[0]=gem;img.color=UINT32_C(0xff2d7c91);
    CHECK(g2d_solid_fill(g2d,&img,0,0,w,h)==0,"g2d_solid_fill_gralloc_bo");
    CHECK(g2d_exec(g2d)==0,"synchronous_real_g2d_dma_write");
    ANativeWindowBuffer nb;
    memset(&nb,0,sizeof(nb));
    nb.common.magic=ANDROID_NATIVE_BUFFER_MAGIC;
    nb.common.version=sizeof(nb);
    nb.common.incRef=buffer_inc;
    nb.common.decRef=buffer_dec;
    nb.width=(int)w;nb.height=(int)h;nb.stride=stride;
    nb.format=HAL_PIXEL_FORMAT_BGRA_8888;
    nb.usage_deprecated=usage;nb.usage=(uint64_t)(unsigned)usage;
    nb.handle=handle;
    rc=gpu_sample_g2d_native_buffer(&nb);
cleanup:
    if(g2d)g2d_fini(g2d);
    if(gem) {struct drm_gem_close arg={.handle=gem};drmIoctl(fd,DRM_IOCTL_GEM_CLOSE,&arg);}
    if(fd>=0)close(fd);
    if(handle && alloc)alloc->free(alloc,handle);
    if(alloc)gralloc_close(alloc);
    return rc;
}
