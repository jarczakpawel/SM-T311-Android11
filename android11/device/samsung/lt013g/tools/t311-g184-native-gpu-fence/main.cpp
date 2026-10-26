#define LOG_TAG "T311G184"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <fstream>

static long long now_us() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long) ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}
static GLuint compile(GLenum type, const char *text) {
    GLuint sh=glCreateShader(type);
    glShaderSource(sh,1,&text,nullptr);
    glCompileShader(sh);
    GLint ok=0;glGetShaderiv(sh,GL_COMPILE_STATUS,&ok);
    if (!ok){puts("G184_SHADER_COMPILE_FAIL");return 0;}
    return sh;
}
int main() {
    setbuf(stdout,nullptr);
    EGLDisplay d=eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint maj=0,min=0;
    if(d==EGL_NO_DISPLAY || !eglInitialize(d,&maj,&min))return 2;
    const char *extensions=eglQueryString(d,EGL_EXTENSIONS);
    printf("G184_EGL_VENDOR=%s\n",eglQueryString(d,EGL_VENDOR));
    printf("G184_EGL_EXTENSION_STRING=%s\n", extensions ? extensions : "NULL");
    const bool nativeFence=extensions && strstr(extensions,"EGL_ANDROID_native_fence_sync");
    const bool khrFence=extensions && strstr(extensions,"EGL_KHR_fence_sync");
    if(!khrFence) {puts("G184_NO_EGL_KHR_FENCE_SYNC");return 3;}
    printf("G184_FENCE_PATH=%s\n",nativeFence?"NATIVE_SYNC_FD":"EGL_KHR_FENCE_SYNC_CPU_WAIT");
    std::ifstream maps("/proc/self/maps");
    for(std::string line;std::getline(maps,line);) {
      if(line.find("libEGL_mesa.so")!=std::string::npos || line.find("libgallium_dri.so")!=std::string::npos) {
        printf("G184_EGL_BACKEND_MAP=%s\n",line.c_str());
        if(line.find("libgallium_dri.so")!=std::string::npos)break;
      }
    }
    EGLint config[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,
                     EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_NONE};
    EGLConfig cfg{};EGLint found=0;
    if(!eglChooseConfig(d,config,&cfg,1,&found)||found!=1)return 4;
    EGLint attr[]={EGL_WIDTH,800,EGL_HEIGHT,1280,EGL_NONE};
    EGLSurface surf=eglCreatePbufferSurface(d,cfg,attr);
    if(surf==EGL_NO_SURFACE)return 5;
    EGLint ca[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
    EGLContext ctx=eglCreateContext(d,cfg,EGL_NO_CONTEXT,ca);
    if(ctx==EGL_NO_CONTEXT || !eglMakeCurrent(d,surf,surf,ctx))return 6;
    static const char* vs="attribute vec2 position; void main() { gl_Position=vec4(position,0.0,1.0); }";
    static const char* fs="precision mediump float; uniform vec4 color; void main(){gl_FragColor=color;}";
    GLuint v=compile(GL_VERTEX_SHADER,vs),f=compile(GL_FRAGMENT_SHADER,fs);
    if(!v||!f)return 7;
    GLuint prog=glCreateProgram();
    glAttachShader(prog,v);glAttachShader(prog,f);glLinkProgram(prog);
    GLint link=0;glGetProgramiv(prog,GL_LINK_STATUS,&link);
    if(!link)return 8;
    glUseProgram(prog);
    GLint loc=glGetAttribLocation(prog,"position");
    GLint col=glGetUniformLocation(prog,"color");
    const GLfloat pts[]={-1.0f,-1.0f,3.0f,-1.0f,-1.0f,3.0f};
    glVertexAttribPointer((GLuint)loc,2,GL_FLOAT,GL_FALSE,0,pts);
    glEnableVertexAttribArray((GLuint)loc);
    glViewport(0,0,800,1280);
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    auto createSync=(PFNEGLCREATESYNCKHRPROC)eglGetProcAddress("eglCreateSyncKHR");
    auto dupFence=(PFNEGLDUPNATIVEFENCEFDANDROIDPROC)eglGetProcAddress("eglDupNativeFenceFDANDROID");
    auto destroySync=(PFNEGLDESTROYSYNCKHRPROC)eglGetProcAddress("eglDestroySyncKHR");
    auto waitSync=(PFNEGLCLIENTWAITSYNCKHRPROC)eglGetProcAddress("eglClientWaitSyncKHR");
    if(!createSync||!destroySync||!waitSync|| (nativeFence && !dupFence))return 9;
    printf("G184_EGL=%d.%d G184_GL=%s\n",maj,min,glGetString(GL_RENDERER));
    int total=0;
    for(int draws: {1,4,16}) {
      for(int i=0;i<9;i++) {
        const long long begin=now_us();
        for(int j=0;j<draws;j++) {
          glUniform4f(col,0.6f,0.2f,0.8f,0.5f);
          glDrawArrays(GL_TRIANGLES,0,3);
        }
        if(glGetError()!=GL_NO_ERROR)return 10;
        EGLSyncKHR sync=createSync(d,nativeFence?EGL_SYNC_NATIVE_FENCE_ANDROID:EGL_SYNC_FENCE_KHR,nullptr);
        if(sync==EGL_NO_SYNC_KHR){printf("G184_SYNC_CREATE_FAIL%x\n",eglGetError());return 11;}
        glFlush();
        if(nativeFence){
          int fd=dupFence(d,sync);
          destroySync(d,sync);
          if(fd<0){printf("G184_EXPORT_FAIL%x\n",eglGetError());return 12;}
          const long long exported=now_us();
          struct pollfd pf{fd,POLLIN,0};
          int immediate=poll(&pf,1,0);
          pf.revents=0;
          int result=poll(&pf,1,1500);
          const long long done=now_us();
          printf("G184_FENCE path=native draws=%d run=%d export_us=%lld signaled_immediate=%d poll_result=%d revents=%d fence_wait_us=%lld total_us=%lld\n",
                draws,i,exported-begin,immediate,result,(int)pf.revents,done-exported,done-begin);
          close(fd);
          if(result!=1 || (pf.revents & POLLERR))return 13;
        }else{
          EGLint immediate=waitSync(d,sync,0,0);
          const long long submitted=now_us();
          EGLint status=waitSync(d,sync,EGL_SYNC_FLUSH_COMMANDS_BIT_KHR,1000000000ull);
          const long long done=now_us();
          destroySync(d,sync);
          printf("G184_FENCE path=khr draws=%d run=%d submit_us=%lld immediate=%d status=%d fence_wait_us=%lld total_us=%lld\n",
                draws,i,submitted-begin,immediate,status,done-submitted,done-begin);
          if(status!=EGL_CONDITION_SATISFIED_KHR)return 14;
        }
        total++;
      }
    }
    printf("G184_NATIVE_GPU_FENCE_PASS=%d\n",total);
    eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    eglDestroyContext(d,ctx);
    eglDestroySurface(d,surf);
    eglTerminate(d);
    return 0;
}
