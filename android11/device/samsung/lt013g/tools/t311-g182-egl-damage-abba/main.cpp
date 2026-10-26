#define LOG_TAG "T311G182"
#include <binder/ProcessState.h>
#include <gui/SurfaceComposerClient.h>
#include <gui/SurfaceControl.h>
#include <gui/Surface.h>
#include <gui/ISurfaceComposerClient.h>
#include <ui/PixelFormat.h>
#include <utils/String8.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
#include <time.h>
using namespace android;
static long long us() {
 timespec t{}; clock_gettime(CLOCK_MONOTONIC,&t);
 return (long long)t.tv_sec*1000000 + t.tv_nsec/1000;
}
static void map_egl(){
 std::ifstream f("/proc/self/maps");
 for(std::string line;std::getline(f,line);){
  if(line.find("/libEGL.so")!=std::string::npos) {
   printf("G182_EGL_MAP=%s\n",line.c_str());break;
  }
 }
}
int main(){
 setbuf(stdout,nullptr);
 map_egl();
 ProcessState::self()->startThreadPool();
 sp<SurfaceComposerClient> client = new SurfaceComposerClient();
 if(client->initCheck()!=OK)return 2;
 sp<SurfaceControl> sc=client->createSurface(String8("T311_G182_HIDDEN_DAMAGE_64X64"),64,64,
                  PIXEL_FORMAT_RGBA_8888,ISurfaceComposerClient::eHidden);
 if(sc==nullptr || !sc->isValid()){client->dispose();return 3;}
 sp<Surface> window=sc->getSurface();
 EGLDisplay d=eglGetDisplay(EGL_DEFAULT_DISPLAY);
 EGLint major=0,minor=0;
 if(d==EGL_NO_DISPLAY || !eglInitialize(d,&major,&minor)) {
  printf("G182_EGL_INITIALIZE_FAIL=%x\n",eglGetError());return 4;
 }
 EGLint attrib[]={EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_SURFACE_TYPE,EGL_WINDOW_BIT,
                 EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_NONE};
 EGLConfig cfg{};EGLint num=0;
 if(!eglChooseConfig(d,attrib,&cfg,1,&num)||num<1){printf("G182_CONFIG_ERROR=%x\n",eglGetError());return 5;}
 EGLSurface surf=eglCreateWindowSurface(d,cfg,window.get(),nullptr);
 if(surf==EGL_NO_SURFACE){printf("G182_SURFACE_ERROR=%x\n",eglGetError());return 6;}
 EGLint ctxattr[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
 EGLContext ctx=eglCreateContext(d,cfg,EGL_NO_CONTEXT,ctxattr);
 if(ctx==EGL_NO_CONTEXT || !eglMakeCurrent(d,surf,surf,ctx)){printf("G182_CTX_ERROR=%x\n",eglGetError());return 7;}
 auto swap=(PFNEGLSWAPBUFFERSWITHDAMAGEKHRPROC)eglGetProcAddress("eglSwapBuffersWithDamageKHR");
 if(!swap){puts("G182_DAMAGE_ENTRY_MISSING");return 8;}
 printf("G182_EGL=%d.%d G182_DAMAGE_ENTRY_OK=1\n",major,minor);
 glViewport(0,0,64,64);
 for(int count:{1,4,16,64}){
  std::vector<EGLint> rects((size_t)count*4);
  for(int j=0;j<count;j++){
   int col=(j%8)*8, row=((j/8)%8)*8;
   rects[(size_t)j*4]=col;
   rects[(size_t)j*4+1]=row;
   rects[(size_t)j*4+2]=8;
   rects[(size_t)j*4+3]=8;
  }
  std::vector<long long> timings;
  int ok=0;
  for(int i=0;i<36;i++){
   glClearColor((i%3)*0.33f,0.25f,0.55f,1.0f);
   glClear(GL_COLOR_BUFFER_BIT);
   auto t0=us();
   EGLBoolean result=swap(d,surf,rects.data(),count);
   auto t1=us();
   if(!result){printf("G182_DAMAGE_FAILED_n%d_i%d_error%x\n",count,i,eglGetError());return 9;}
   if(i>=4)timings.push_back(t1-t0);
   ok++;
  }
  std::sort(timings.begin(),timings.end());
  printf("G182_DAMAGE n=%d successful=%d p50us=%lld p95us=%lld min=%lld max=%lld\n",
      count,ok,timings[timings.size()/2],timings[(timings.size()*95)/100],
      timings.front(),timings.back());
 }
 eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
 eglDestroyContext(d,ctx);
 eglDestroySurface(d,surf);
 eglTerminate(d);
 window.clear();sc.clear();client->dispose();
 puts("G182_HIDDEN_SURFACE_CLEAN_EXIT=1");
 return 0;
}
