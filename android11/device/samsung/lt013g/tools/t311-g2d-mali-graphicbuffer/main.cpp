/* SM-T311 actual platform GraphicBuffer + Exynos G2D + Mali GLES2.
 * Uses the official Android RefBase and gralloc allocator; never forges
 * ANativeWindowBuffer headers. PRIVATE pixel test only, no FIMD/SurfaceFlinger.
 */
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <hardware/gralloc.h>
#include <ui/GraphicBuffer.h>
#include <ui/PixelFormat.h>
#include <utils/StrongPointer.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <xf86drm.h>
#include <drm.h>
#include <drm_fourcc.h>
extern "C" {
#include "exynos_drm.h"
#include "exynos_fimg2d.h"
}

static int sample_buffer_with_mali(android::GraphicBuffer* gb, uint32_t expected) {
  EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (display == EGL_NO_DISPLAY || !eglInitialize(display,nullptr,nullptr)) {
    printf("EGL_INIT_ERROR=0x%x\n",eglGetError());return 11;
  }
  const char* exts=eglQueryString(display,EGL_EXTENSIONS);
  printf("ANDROID_NATIVE_BUFFER_EGL=%s\n",exts&&strstr(exts,"EGL_ANDROID_image_native_buffer")?"YES":"NO");
  EGLint config_attr[]={EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,
     EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_NONE};
  EGLConfig cfg; EGLint configs=0;
  if(!eglChooseConfig(display,config_attr,&cfg,1,&configs)||!configs)return 12;
  EGLint pb_attr[]={EGL_WIDTH,2,EGL_HEIGHT,2,EGL_NONE};
  EGLSurface pb=eglCreatePbufferSurface(display,cfg,pb_attr);
  EGLint ctx_attr[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
  EGLContext context=eglCreateContext(display,cfg,EGL_NO_CONTEXT,ctx_attr);
  if(pb==EGL_NO_SURFACE||context==EGL_NO_CONTEXT||!eglMakeCurrent(display,pb,pb,context))return 13;
  printf("TRUE_GLES_RENDERER=%s\n",glGetString(GL_RENDERER));
  auto create=reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(eglGetProcAddress("eglCreateImageKHR"));
  auto destroy=reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(eglGetProcAddress("eglDestroyImageKHR"));
  auto target=reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(eglGetProcAddress("glEGLImageTargetTexture2DOES"));
  if(!create||!destroy||!target)return 14;
  EGLint image_attrs[]={EGL_NONE};
  EGLImageKHR image=create(display,EGL_NO_CONTEXT,EGL_NATIVE_BUFFER_ANDROID,
                           reinterpret_cast<EGLClientBuffer>(gb->getNativeBuffer()),image_attrs);
  if(image==EGL_NO_IMAGE_KHR){printf("REAL_GRAPHICBUFFER_EGL_IMAGE_FAILED=0x%x\n",eglGetError());return 15;}
  GLuint tex=0,vs=0,fs=0,program=0,vbo=0;
  glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
  target(GL_TEXTURE_2D,reinterpret_cast<GLeglImageOES>(image));
  GLenum error=glGetError();
  if(error){printf("GLES_REAL_GRAPHICBUFFER_IMAGE_TARGET_ERROR=0x%x\n",error);destroy(display,image);return 16;}
  const char* vertex="attribute vec2 a;void main(){gl_Position=vec4(a,0.0,1.0);}";
  const char* fragment="precision mediump float;uniform sampler2D tex;void main(){gl_FragColor=texture2D(tex,vec2(0.5,0.5));}";
  vs=glCreateShader(GL_VERTEX_SHADER);glShaderSource(vs,1,&vertex,nullptr);glCompileShader(vs);
  fs=glCreateShader(GL_FRAGMENT_SHADER);glShaderSource(fs,1,&fragment,nullptr);glCompileShader(fs);
  GLint success=0;glGetShaderiv(vs,GL_COMPILE_STATUS,&success);if(!success)return 17;
  glGetShaderiv(fs,GL_COMPILE_STATUS,&success);if(!success)return 18;
  program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);
  glBindAttribLocation(program,0,"a");glLinkProgram(program);glGetProgramiv(program,GL_LINK_STATUS,&success);
  if(!success)return 19;
  constexpr GLfloat verts[]={-1.0f,-1.0f,1.0f,-1.0f,-1.0f,1.0f,1.0f,1.0f};
  glGenBuffers(1,&vbo);glBindBuffer(GL_ARRAY_BUFFER,vbo);glBufferData(GL_ARRAY_BUFFER,sizeof(verts),verts,GL_STATIC_DRAW);
  glUseProgram(program);glUniform1i(glGetUniformLocation(program,"tex"),0);
  glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,nullptr);glEnableVertexAttribArray(0);
  glViewport(0,0,2,2);glDrawArrays(GL_TRIANGLE_STRIP,0,4);
  unsigned char pixel[4]={0};glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
  glFinish();error=glGetError();
  printf("G2D_TO_MALI_SAMPLED_PIXEL=%02x%02x%02x%02x GLES_ERROR=0x%x\n",pixel[0],pixel[1],pixel[2],pixel[3],error);
  glDeleteBuffers(1,&vbo);glDeleteProgram(program);glDeleteShader(vs);glDeleteShader(fs);
  glDeleteTextures(1,&tex);destroy(display,image);
  eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
  eglDestroyContext(display,context);eglDestroySurface(display,pb);eglTerminate(display);
  uint32_t result=((uint32_t)pixel[3]<<24)|((uint32_t)pixel[0]<<16)|((uint32_t)pixel[1]<<8)|pixel[2];
  printf("REAL_G2D_TO_MALI_GLES_PIXEL_MATCH=%s EXPECT=%08x OBSERVED=%08x\n",
         (!error && result==expected)?"PASS":"FAIL",expected,result);
  return (!error && result==expected)?0:20;
}
int main(int argc,char**argv) {
  const uint32_t w=(argc==2)?static_cast<uint32_t>(atoi(argv[1])):64;
  if(w<32||w>256||(w%16))return 2;
  const uint32_t h=w;
  const uint64_t usage=GRALLOC_USAGE_HW_CAMERA_WRITE|GRALLOC_USAGE_HW_TEXTURE|
       GRALLOC_USAGE_SW_READ_OFTEN|GRALLOC_USAGE_SW_WRITE_OFTEN;
  android::sp<android::GraphicBuffer> gb=new android::GraphicBuffer(w,h,android::PIXEL_FORMAT_BGRA_8888,1,usage,"SM-T311 offline G2D GLES");
  if(!gb || gb->initCheck()!=android::NO_ERROR){printf("REAL_GRAPHICBUFFER_ALLOC_FAIL=%d\n",gb?gb->initCheck():-1);return 3;}
  const native_handle_t* nh=gb->handle;
  if(!nh || nh->numFds!=1 || nh->data[0]<0){printf("REAL_GRAPHICBUFFER_INVALID_HANDLE\n");return 4;}
  printf("GRAPHICBUFFER_REAL_ALLOC=%ux%u STRIDE=%u FD=%d\n",w,h,gb->getStride(),nh->data[0]);
  int fd=open("/dev/dri/card0",O_RDWR|O_CLOEXEC);
  if(fd<0)return 5;
  uint32_t handle=0;
  if(drmPrimeFDToHandle(fd,nh->data[0],&handle)<0||!handle)return 6;
  struct drm_exynos_gem_info info={};info.handle=handle;
  if(drmIoctl(fd,DRM_IOCTL_EXYNOS_GEM_GET,&info)<0 || (info.flags&0x18)!=0x18){
    printf("G2D_GRAPHICBUFFER_COHERENCY_FLAGS=0x%x INVALID\n",info.flags);return 7;
  }
  struct g2d_context*ctx=g2d_init(fd);
  if(!ctx)return 8;
  struct g2d_image img={};img.width=w;img.height=h;img.stride=gb->getStride()*4;
  img.color_mode=static_cast<enum e_g2d_color_mode>(G2D_COLOR_FMT_ARGB8888|G2D_ORDER_AXRGB);
  img.buf_type=G2D_IMGBUF_GEM;img.bo[0]=handle;
  constexpr uint32_t color=UINT32_C(0xff2d7c91);img.color=color;
  if(g2d_solid_fill(ctx,&img,0,0,w,h)<0||g2d_exec(ctx)<0){printf("G2D_FILL_FAIL\n");return 9;}
  void*data=nullptr;
  if(gb->lock(GRALLOC_USAGE_SW_READ_OFTEN,&data)!=android::NO_ERROR||!data)return 10;
  uint32_t first=static_cast<uint32_t*>(data)[0];
  gb->unlock();
  printf("REAL_GRAPHICBUFFER_G2D_PIXEL0=%08x\n",first);
  if(first!=color)return 11;
  int result=sample_buffer_with_mali(gb.get(),color);
  g2d_fini(ctx);
  struct drm_gem_close gemclose={};gemclose.handle=handle;
  drmIoctl(fd,DRM_IOCTL_GEM_CLOSE,&gemclose);close(fd);
  return result;
}
