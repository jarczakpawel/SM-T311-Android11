#define LOG_TAG "T311G179"
#include <gui/SurfaceComposerClient.h>
#include <gui/SurfaceControl.h>
#include <gui/ISurfaceComposerClient.h>
#include <gui/IGraphicBufferProducer.h>
#include <gui/IProducerListener.h>
#include <binder/IBinder.h>
#include <binder/IInterface.h>
#include <binder/ProcessState.h>
#include <ui/GraphicBuffer.h>
#include <utils/String8.h>
#include <utils/Timers.h>
#include <system/window.h>
#include <cstdio>
#include <fstream>
#include <string>
#include <inttypes.h>

using namespace android;
static void show_loaded_gui() {
    std::ifstream f("/proc/self/maps");
    std::string line;
    while(std::getline(f,line)) {
        if(line.find("/libgui.so") != std::string::npos) {
            printf("G179_LIBGUI_MAP=%s\n",line.c_str());
            break;
        }
    }
}
int main(int argc, char **argv) {
    const bool stall_mode = argc==2 && std::string(argv[1])=="stall";
    setbuf(stdout,nullptr);
    show_loaded_gui();
    ProcessState::self()->startThreadPool();
    sp<SurfaceComposerClient> client = new SurfaceComposerClient();
    status_t check=client->initCheck();
    printf("G179_SF_COMPOSER_INIT=%d\n",check);
    if(check!=OK) return 2;
    sp<SurfaceControl> control = client->createSurface(
        String8("T311_G179_OFFSCREEN_1X1"),1,1,
        PIXEL_FORMAT_RGBA_8888,ISurfaceComposerClient::eHidden);
    if(control==nullptr || !control->isValid()) {
        puts("G179_HIDDEN_SURFACE_CREATE_FAILED");
        client->dispose();
        return 3;
    }
    puts("G179_HIDDEN_SURFACE_CREATED");
    sp<IGraphicBufferProducer> producer = control->getIGraphicBufferProducer();
    if(producer==nullptr) {control.clear(); client->dispose(); return 4;}
    sp<IBinder> binder = IInterface::asBinder(producer);
    bool remote = binder!=nullptr && binder->remoteBinder()!=nullptr &&
                  binder->localBinder()==nullptr;
    printf("G179_BP_REMOTE_PRODUCER=%d\n",remote?1:0);
    if (!remote) {control.clear(); client->dispose(); return 5;}
    IGraphicBufferProducer::QueueBufferOutput output;
    status_t rc=producer->connect(nullptr,NATIVE_WINDOW_API_CPU,false,&output);
    printf("G179_PRODUCER_CONNECT=%d\n",rc);
    if(rc!=OK){control.clear();client->dispose();return 6;}
    rc=producer->setDequeueTimeout(ms2ns(stall_mode?70:90));
    printf("G179_TIMEOUT_CONFIG=%d\n",rc);
    if(rc!=OK){producer->disconnect(NATIVE_WINDOW_API_CPU,IGraphicBufferProducer::DisconnectMode::Api);control.clear();client->dispose();return 7;}
    IGraphicBufferProducer::QueueBufferInput queued(0,false,HAL_DATASPACE_UNKNOWN,
            Rect(0,0,1,1),NATIVE_WINDOW_SCALING_MODE_FREEZE,0,Fence::NO_FENCE);
    int ok=0;
    bool stalled=false;
    for(int i=0;i<(stall_mode?5:8);i++) {
        int slot=-1;
        sp<Fence> fence;
        nsecs_t t=systemTime(SYSTEM_TIME_MONOTONIC);
        int result=producer->dequeueBuffer(&slot,&fence,1,1,PIXEL_FORMAT_RGBA_8888,
                                          GRALLOC_USAGE_SW_WRITE_OFTEN,nullptr,nullptr);
        int64_t us=(systemTime(SYSTEM_TIME_MONOTONIC)-t)/1000;
        printf("G179_REMOTE_DEQUEUE index=%d rc=%d slot=%d binder_and_other_us=%" PRId64 "\n",
               i,result,slot,us);
        if(result==TIMED_OUT && stall_mode) {stalled=true;break;}
        if(result<0) break;
        if(result & IGraphicBufferProducer::BUFFER_NEEDS_REALLOCATION) {
            sp<GraphicBuffer> buffer;
            rc=producer->requestBuffer(slot,&buffer);
            printf("G179_REMOTE_REQUEST rc=%d\n",rc);
            if(rc!=OK) break;
        }
        if (stall_mode) {
            rc=producer->queueBuffer(slot,queued,&output);
            printf("G179_REMOTE_QUEUE rc=%d\n",rc);
        } else {
            rc=producer->cancelBuffer(slot,Fence::NO_FENCE);
            printf("G179_REMOTE_CANCEL rc=%d\n",rc);
        }
        if(rc!=OK) break;
        ++ok;
    }
    rc=producer->disconnect(NATIVE_WINDOW_API_CPU,IGraphicBufferProducer::DisconnectMode::Api);
    printf("G179_PRODUCER_DISCONNECT=%d\n",rc);
    control.clear();
    control.clear();
    client->dispose();
    puts("G179_HIDDEN_SURFACE_DESTROYED");
    printf("G179_DEQUEUE_SUCCESS_COUNT=%d STALLED=%d\n",ok,stalled?1:0);
    return stall_mode ? (stalled?0:9) : (ok==8?0:8);
}
