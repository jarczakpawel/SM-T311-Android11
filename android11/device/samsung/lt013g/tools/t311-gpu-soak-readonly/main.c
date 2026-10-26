// Samsung SM-T311 8s capped offscreen Mali-400/Lima GLES2 workload.
// Not an FPS benchmark: EGL pbuffer glFinish is offscreen and has no VBlank.
#define _POSIX_C_SOURCE 200809L
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static double monotonic(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC,&t);
    return (double)t.tv_sec+(double)t.tv_nsec/1.e9;
}
static GLuint make_shader(GLenum kind,const char* source) {
    GLuint shader=glCreateShader(kind);
    if (!shader) return 0;
    glShaderSource(shader,1,&source,NULL);
    glCompileShader(shader);
    GLint ok=0;
    glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if(!ok) {
        char log[512]={0};
        glGetShaderInfoLog(shader,sizeof(log),NULL,log);
        fprintf(stderr,"SHADER_ERROR %s\n",log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}
int main(int argc,char **argv) {
    int seconds=8;
    if(argc>1) seconds=atoi(argv[1]);
    if(seconds<1||seconds>12) {fputs("BOUNDED_SECONDS_1_TO_12_REQUIRED\n",stderr);return 22;}
    int result=1;
    EGLDisplay d=EGL_NO_DISPLAY;
    EGLSurface surf=EGL_NO_SURFACE;
    EGLContext ctx=EGL_NO_CONTEXT;
    GLuint vs=0,fs=0,program=0,vertex_buffer=0;
    d=eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if(d==EGL_NO_DISPLAY || !eglInitialize(d,NULL,NULL)) {
        fprintf(stderr,"EGL_INITIALIZE_ERROR=0x%x\n",eglGetError()); goto done;
    }
    EGLConfig conf;
    EGLint n=0;
    EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,
                   EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_NONE};
    if(!eglChooseConfig(d,attrs,&conf,1,&n)||n!=1) goto done;
    EGLint pa[]={EGL_WIDTH,256,EGL_HEIGHT,256,EGL_NONE};
    surf=eglCreatePbufferSurface(d,conf,pa);
    EGLint ca[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
    if(!eglBindAPI(EGL_OPENGL_ES_API)) goto done;
    ctx=eglCreateContext(d,conf,EGL_NO_CONTEXT,ca);
    if(surf==EGL_NO_SURFACE || ctx==EGL_NO_CONTEXT ||
       !eglMakeCurrent(d,surf,surf,ctx)) goto done;
    printf("GPU_RENDERER=%s\n",glGetString(GL_RENDERER));
    printf("GPU_VERSION=%s\n",glGetString(GL_VERSION));
    static const char *vertex=
        "attribute vec2 a;varying vec2 uv;"
        "void main(){uv=(a+1.0)*0.5;gl_Position=vec4(a,0.0,1.0);}";
    static const char *fragment=
        "precision mediump float;varying vec2 uv;uniform float phase;"
        "void main(){float r=uv.x+phase;float g=uv.y;float b=0.2;"
        "for(int i=0;i<12;++i){r=fract(r*1.013+g*0.041);"
        "g=fract(g*1.031+b*0.051);b=fract(b*1.027+r*0.031);}"
        "gl_FragColor=vec4(r,g,b,1.0);}";
    vs=make_shader(GL_VERTEX_SHADER,vertex);
    fs=make_shader(GL_FRAGMENT_SHADER,fragment);
    if(!vs||!fs)goto done;
    program=glCreateProgram();
    if(!program)goto done;
    glAttachShader(program,vs);
    glAttachShader(program,fs);
    glBindAttribLocation(program,0,"a");
    glLinkProgram(program);
    GLint ok=0;
    glGetProgramiv(program,GL_LINK_STATUS,&ok);
    if(!ok)goto done;
    const GLfloat coords[]={-1.f,-1.f,1.f,-1.f,-1.f,1.f,1.f,1.f};
    glGenBuffers(1,&vertex_buffer);
    glBindBuffer(GL_ARRAY_BUFFER,vertex_buffer);
    glBufferData(GL_ARRAY_BUFFER,sizeof(coords),coords,GL_STATIC_DRAW);
    glUseProgram(program);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,0);
    glEnableVertexAttribArray(0);
    GLint location=glGetUniformLocation(program,"phase");
    glViewport(0,0,256,256);
    glDisable(GL_BLEND);
    uint32_t frames=0;
    double first=monotonic(),limit=first+seconds;
    while(monotonic()<limit) {
        glUniform1f(location,(float)(frames%401)/401.f);
        glDrawArrays(GL_TRIANGLE_STRIP,0,4);
        // Enforces GPU completion for each sample. No unbounded queue.
        glFinish();
        frames++;
        if((frames%64)==0 && glGetError()!=GL_NO_ERROR) {
            fputs("GL_FAILURE\n",stderr);goto done;
        }
    }
    double elapsed=monotonic()-first;
    uint8_t pixel[4]={0};
    glReadPixels(128,128,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    GLenum err=glGetError();
    printf("OFFSCREEN_COMPLETED_FRAMES=%u ELAPSED_SECONDS=%.3f FRAMES_PER_SECOND=%.2f\n",
           frames,elapsed,frames/elapsed);
    printf("GPU_PIXEL_SAMPLE_RGBA=%u,%u,%u,%u GL_ERROR=0x%x\n",
           pixel[0],pixel[1],pixel[2],pixel[3],(unsigned)err);
    if(err==GL_NO_ERROR && frames>5 && pixel[3]>=254) {
        result=0;
        puts("SM_T311_OFFSCREEN_GPU_8S_SOAK=PASS");
    }
done:
    if(vertex_buffer)glDeleteBuffers(1,&vertex_buffer);
    if(program)glDeleteProgram(program);
    if(vs)glDeleteShader(vs);
    if(fs)glDeleteShader(fs);
    if(d!=EGL_NO_DISPLAY) {
        eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
        if(ctx!=EGL_NO_CONTEXT)eglDestroyContext(d,ctx);
        if(surf!=EGL_NO_SURFACE)eglDestroySurface(d,surf);
        eglTerminate(d);
    }
    if(result!=0)fprintf(stderr,"GPU_OFFSCREEN_FAIL EGL=0x%x\n",eglGetError());
    return result;
}
