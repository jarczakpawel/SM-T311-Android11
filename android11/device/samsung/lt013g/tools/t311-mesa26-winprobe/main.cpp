#include <gui/SurfaceComposerClient.h>
#include <gui/SurfaceControl.h>
#include <gui/Surface.h>
#include <ui/PixelFormat.h>
#include <system/window.h>
#include <utils/String8.h>
#include <utils/Errors.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <dlfcn.h>
#include <unistd.h>
#include <cstdio>
#include <cstdint>
using namespace android;
#define LOAD(H,F) \
  auto F##_26 = reinterpret_cast<decltype(&F)>(dlsym(H,#F)); \
  if(!F##_26) {fprintf(stderr,"WINPROBE_SYMBOL_MISSING=%s\n",#F);return 40;}

int main(int argc,char**argv) {
    const char* dir=argc>1?argv[1]:"/data/local/tmp/t311-mesa26prod";
    fprintf(stderr,"WINPROBE_BEGIN=%s\n",dir);
    sp<SurfaceComposerClient> client=new SurfaceComposerClient();
    if(!client || client->initCheck()!=OK) {
        fprintf(stderr,"WINPROBE_CONNECT_SURFACE_COMPOSER=FAIL code=%d\n",
                client?client->initCheck():-1);
        return 1;
    }
    sp<SurfaceControl> sc=client->createSurface(String8("SMT311 Mesa26 winprobe"),128,128,PIXEL_FORMAT_RGBA_8888,0);
    if(!sc || !sc->isValid()) {
        fprintf(stderr,"WINPROBE_SURFACE_CREATE=FAIL\n");return 2;
    }
    SurfaceComposerClient::Transaction txn;
    status_t st=txn.setLayer(sc,1500).setPosition(sc,16,64).show(sc).apply(true);
    if(st!=OK) {
        fprintf(stderr,"WINPROBE_SURFACE_SHOW=FAIL code=%d\n",st);return 3;
    }
    sp<Surface> window=sc->getSurface();
    if(!window) {fprintf(stderr,"WINPROBE_NATIVE_WINDOW_NULL\n");return 4;}
    fprintf(stderr,"WINPROBE_NATIVE_WINDOW=PASS\n");
    ANativeWindow* anw=static_cast<ANativeWindow*>(window.get());
    // Android frameworks/native EGL wrapper normally does this before
    // forwarding the surface to vendor Mesa. This standalone test bypasses
    // the wrapper, so it must mirror its BufferQueue EGL connection.
    int conn=native_window_api_connect(anw,NATIVE_WINDOW_API_EGL);
    if(conn<0){fprintf(stderr,"WINPROBE_NATIVE_WINDOW_CONNECT=FAIL %d\n",conn);return 20;}
    fprintf(stderr,"WINPROBE_NATIVE_WINDOW_CONNECT=PASS\n");
    char path[256];
    snprintf(path,sizeof(path),"%s/libEGL_mesa.so",dir);
    void* e=dlopen(path,RTLD_NOW|RTLD_LOCAL);
    if(!e) {fprintf(stderr,"WINPROBE_EGL_DLOPEN=FAIL %s\n",dlerror());return 5;}
    snprintf(path,sizeof(path),"%s/libGLESv2_mesa.so",dir);
    void* g=dlopen(path,RTLD_NOW|RTLD_LOCAL);
    if(!g){fprintf(stderr,"WINPROBE_GLES_DLOPEN=FAIL %s\n",dlerror());return 6;}
    LOAD(e,eglGetDisplay)
    LOAD(e,eglInitialize)
    LOAD(e,eglChooseConfig)
    LOAD(e,eglGetConfigAttrib)
    LOAD(e,eglBindAPI)
    LOAD(e,eglCreateContext)
    LOAD(e,eglCreateWindowSurface)
    LOAD(e,eglMakeCurrent)
    LOAD(e,eglSwapBuffers)
    LOAD(e,eglDestroyContext)
    LOAD(e,eglDestroySurface)
    LOAD(e,eglTerminate)
    LOAD(e,eglGetError)
    LOAD(g,glViewport)
    LOAD(g,glClearColor)
    LOAD(g,glClear)
    LOAD(g,glGetString)
    LOAD(g,glFinish)
    EGLDisplay d=eglGetDisplay_26(EGL_DEFAULT_DISPLAY);
    if(d==EGL_NO_DISPLAY||!eglInitialize_26(d,nullptr,nullptr)){
        fprintf(stderr,"WINPROBE_EGL_INIT=FAIL %x\n",eglGetError_26());return 7;
    }
    const EGLint attrib[]={EGL_SURFACE_TYPE,EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_NONE};
    EGLConfig cfg=nullptr;EGLint count=0;
    if(!eglChooseConfig_26(d,attrib,&cfg,1,&count)||count<1){
        fprintf(stderr,"WINPROBE_EGL_CONFIG=FAIL %x\n",eglGetError_26());return 8;
    }
    EGLint visual=0;
    if(!eglGetConfigAttrib_26(d,cfg,EGL_NATIVE_VISUAL_ID,&visual)) {
        fprintf(stderr,"WINPROBE_VISUAL_ID=FAIL %x\n",eglGetError_26());return 21;
    }
    int fmt=native_window_set_buffers_format(anw,visual);
    if(fmt!=0){fprintf(stderr,"WINPROBE_SET_BUFFER_FORMAT=FAIL %d\n",fmt);return 22;}
    fprintf(stderr,"WINPROBE_NATIVE_VISUAL_ID=0x%x\n",visual);
    eglBindAPI_26(EGL_OPENGL_ES_API);
    const EGLint ctxAttrib[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
    EGLContext ctx=eglCreateContext_26(d,cfg,EGL_NO_CONTEXT,ctxAttrib);
    if(ctx==EGL_NO_CONTEXT){fprintf(stderr,"WINPROBE_EGL_CTX=FAIL %x\n",eglGetError_26());return 9;}
    EGLSurface surf=eglCreateWindowSurface_26(d,cfg,static_cast<EGLNativeWindowType>(window.get()),nullptr);
    if(surf==EGL_NO_SURFACE){
        fprintf(stderr,"WINPROBE_EGL_WINDOW=FAIL %x\n",eglGetError_26());return 10;
    }
    if(!eglMakeCurrent_26(d,surf,surf,ctx)){
        fprintf(stderr,"WINPROBE_MAKE_CURRENT=FAIL %x\n",eglGetError_26());return 11;
    }
    fprintf(stderr,"WINPROBE_RENDERER=%s\n",glGetString_26(GL_RENDERER));
    fprintf(stderr,"WINPROBE_VERSION=%s\n",glGetString_26(GL_VERSION));
    bool success=true;
    for(int i=0;i<3;i++){
        glViewport_26(0,0,128,128);
        glClearColor_26(.15f,.4f,.7f,1.0f);
        glClear_26(GL_COLOR_BUFFER_BIT);
        glFinish_26();
        if(!eglSwapBuffers_26(d,surf)){
            fprintf(stderr,"WINPROBE_SWAP=%d FAIL %x\n",i,eglGetError_26());
            success=false;break;
        }
        fprintf(stderr,"WINPROBE_SWAP=%d PASS\n",i);
    }
    if(success) {fprintf(stderr,"WINPROBE_ANDROID_WINDOW=PASS\n");usleep(150000);}
    eglMakeCurrent_26(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    eglDestroySurface_26(d,surf);
    eglDestroyContext_26(d,ctx);
    eglTerminate_26(d);
    native_window_set_buffers_format(anw,0);
    int disconn=native_window_api_disconnect(anw,NATIVE_WINDOW_API_EGL);
    fprintf(stderr,"WINPROBE_NATIVE_WINDOW_DISCONNECT=%d\n",disconn);
    dlclose(g);dlclose(e);
    fprintf(stderr,"WINPROBE_CLEANUP=PASS\n");
    return success?0:12;
}
