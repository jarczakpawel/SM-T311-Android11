#define LOG_TAG "T311G178"
#include <gui/BufferQueue.h>
#include <gui/BufferItem.h>
#include <gui/IConsumerListener.h>
#include <gui/IProducerListener.h>
#include <gui/IGraphicBufferConsumer.h>
#include <gui/IGraphicBufferProducer.h>
#include <ui/GraphicBuffer.h>
#include <utils/Errors.h>
#include <utils/Timers.h>
#include <system/window.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>

using namespace android;

class Listener final : public BnConsumerListener {
 public:
  void onFrameAvailable(const BufferItem&) override {}
  void onBuffersReleased() override {}
  void onSidebandStreamChanged() override {}
};

static void show_loaded_gui() {
  std::ifstream in("/proc/self/maps");
  std::string line;
  while (std::getline(in,line)) {
    if (line.find("/libgui.so") != std::string::npos) {
      printf("G178_LIBGUI_MAPPING=%s\n",line.c_str());
      break;
    }
  }
}

int main(int argc, char** argv) {
  setbuf(stdout,nullptr);
  const bool release_mode = argc==2 && strcmp(argv[1],"release")==0;
  show_loaded_gui();
  sp<IGraphicBufferProducer> producer;
  sp<IGraphicBufferConsumer> consumer;
  BufferQueue::createBufferQueue(&producer,&consumer);
  if (!producer || !consumer) return 2;
  sp<IConsumerListener> listener = new Listener();
  int rc = consumer->consumerConnect(listener,false);
  printf("G178_CONSUMER_CONNECT=%d\n",rc);
  if(rc!=OK) return 3;
  IGraphicBufferProducer::QueueBufferOutput output;
  rc=producer->connect(nullptr,NATIVE_WINDOW_API_CPU,false,&output);
  printf("G178_PRODUCER_CONNECT=%d\n",rc);
  if(rc!=OK) return 4;
  rc=producer->setDequeueTimeout(ms2ns(90));
  printf("G178_SET_TIMEOUT=%d\n",rc);
  if(rc!=OK) return 5;
  IGraphicBufferProducer::QueueBufferInput input(0,false,HAL_DATASPACE_UNKNOWN,
          Rect(0,0,1,1),NATIVE_WINDOW_SCALING_MODE_FREEZE,0,Fence::NO_FENCE);
  bool timedout=false;
  bool woke=false;
  for(int n=0;n<9;++n) {
    int slot=BufferQueue::INVALID_BUFFER_SLOT;
    sp<Fence> fence;
    std::thread releaser;
    if (release_mode && n==2) {
      releaser = std::thread([consumer] {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        BufferItem item;
        int acquired=consumer->acquireBuffer(&item,0);
        int released=acquired==OK?
          consumer->releaseBuffer(item.mSlot,item.mFrameNumber,
            EGL_NO_DISPLAY,EGL_NO_SYNC_KHR,Fence::NO_FENCE) : acquired;
        printf("G178_ASYNC_RELEASE acquired=%d released=%d\n",acquired,released);
      });
    }
    nsecs_t start=systemTime(SYSTEM_TIME_MONOTONIC);
    int status=producer->dequeueBuffer(&slot,&fence,1,1,PIXEL_FORMAT_RGBA_8888,
                    GRALLOC_USAGE_SW_WRITE_OFTEN,nullptr,nullptr);
    long long us=(systemTime(SYSTEM_TIME_MONOTONIC)-start)/1000;
    if(releaser.joinable()) releaser.join();
    printf("G178_DEQUEUE index=%d rc=%d slot=%d elapsed_us=%lld\n",n,status,slot,us);
    if (status==TIMED_OUT) { timedout=true; break; }
    if (status<0) return 6;
    if (status & IGraphicBufferProducer::BUFFER_NEEDS_REALLOCATION) {
      sp<GraphicBuffer> buffer;
      rc=producer->requestBuffer(slot,&buffer);
      printf("G178_REQUEST_BUFFER rc=%d\n",rc);
      if(rc!=OK) return 7;
    }
    rc=producer->queueBuffer(slot,input,&output);
    printf("G178_QUEUE rc=%d\n",rc);
    if (rc!=OK) return 8;
    if (release_mode && n==2) { woke=us>=15000 && us<90000; break; }
  }
  printf("G178_EXPECTED_SLOT_TIMEOUT=%d WAKE_PASS=%d\n",timedout?1:0,woke?1:0);
  producer->disconnect(NATIVE_WINDOW_API_CPU,IGraphicBufferProducer::DisconnectMode::Api);
  consumer->consumerDisconnect();
  return (release_mode?woke:timedout)?0:9;
}
