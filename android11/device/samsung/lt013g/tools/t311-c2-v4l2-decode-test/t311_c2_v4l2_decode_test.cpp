#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cinttypes>
#include <cstdint>
#include <string>
#include <vector>
#include <chrono>
#include <thread>

struct Packet { size_t off=0, size=0; uint32_t flags=0; uint64_t pts=0; };
static bool load(const char* p,std::vector<uint8_t>*v){FILE*f=fopen(p,"rb");if(!f)return false;fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);if(n<=0){fclose(f);return false;}v->resize(n);bool ok=fread(v->data(),1,v->size(),f)==v->size();fclose(f);return ok;}
static bool loadidx(const std::string&p,std::vector<Packet>*ps){FILE*f=fopen(p.c_str(),"r");if(!f)return false;Packet x;while(fscanf(f,"%zu %zu %u %" SCNu64 "\n",&x.off,&x.size,&x.flags,&x.pts)==4)ps->push_back(x);fclose(f);return !ps->empty();}
int main(int argc,char**argv){setvbuf(stdout,nullptr,_IONBF,0);setvbuf(stderr,nullptr,_IONBF,0);printf("T311_C2_DEC_MAIN=ENTER argc=%d\n",argc);if(argc!=5){fprintf(stderr,"usage: %s WIDTH HEIGHT IN.h264 OUT.raw\n",argv[0]);return 2;}int w=atoi(argv[1]),h=atoi(argv[2]);std::vector<uint8_t>data;std::vector<Packet>pkts;if(!load(argv[3],&data)||!loadidx(std::string(argv[3])+".idx",&pkts)){printf("T311_C2_DEC_INPUT=FAIL\n");return 3;}AMediaCodec*c=AMediaCodec_createCodecByName("c2.v4l2.avc.decoder");if(!c){printf("T311_C2_DEC_CREATE=FAIL\n");return 4;}printf("T311_C2_DEC_CREATE=PASS packets=%zu bytes=%zu\n",pkts.size(),data.size());AMediaFormat*f=AMediaFormat_new();AMediaFormat_setString(f,"mime","video/avc");AMediaFormat_setInt32(f,"width",w);AMediaFormat_setInt32(f,"height",h);AMediaFormat_setInt32(f,"color-format",19);media_status_t r=AMediaCodec_configure(c,f,nullptr,nullptr,0);printf("T311_C2_DEC_CONFIGURE=%s status=%d format=%s\n",r==AMEDIA_OK?"PASS":"FAIL",r,AMediaFormat_toString(f));AMediaFormat_delete(f);if(r!=AMEDIA_OK){AMediaCodec_delete(c);return 5;}r=AMediaCodec_start(c);printf("T311_C2_DEC_START=%s status=%d\n",r==AMEDIA_OK?"PASS":"FAIL",r);if(r!=AMEDIA_OK){AMediaCodec_delete(c);return 6;}FILE*out=fopen(argv[4],"wb");if(!out){AMediaCodec_stop(c);AMediaCodec_delete(c);return 7;}size_t pi=0,obytes=0;int obufs=0;bool eosq=false,eos=false;const char* timeoutEnv=getenv("T311_DECODE_TIMEOUT_SEC");int timeoutSec=timeoutEnv?atoi(timeoutEnv):45;if(timeoutSec<10||timeoutSec>240)timeoutSec=45;printf("T311_C2_DEC_TIMEOUT_SEC=%d\n",timeoutSec);auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(timeoutSec);while(!eos&&std::chrono::steady_clock::now()<deadline){if(!eosq){ssize_t ix=AMediaCodec_dequeueInputBuffer(c,10000);if(ix>=0){size_t cap=0;uint8_t*b=AMediaCodec_getInputBuffer(c,ix,&cap);if(pi<pkts.size()){auto&p=pkts[pi];if(p.off+p.size>data.size()||p.size>cap){printf("T311_C2_DEC_PACKET=FAIL idx=%zu size=%zu cap=%zu\n",pi,p.size,cap);return 8;}memcpy(b,data.data()+p.off,p.size);uint32_t fl=(p.flags&AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG)?AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG:0;r=AMediaCodec_queueInputBuffer(c,ix,0,p.size,p.pts,fl);if(r!=AMEDIA_OK)return 9;pi++;}else{r=AMediaCodec_queueInputBuffer(c,ix,0,0,pkts.back().pts+33333,AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);if(r!=AMEDIA_OK)return 10;eosq=true;printf("T311_C2_DEC_INPUT_EOS=PASS packets=%zu\n",pi);}}}
AMediaCodecBufferInfo info{};ssize_t oi=AMediaCodec_dequeueOutputBuffer(c,&info,10000);if(oi>=0){const char* delayEnv=getenv("T311_DECODE_OUTPUT_DELAY_US");int delayUs=delayEnv?atoi(delayEnv):0;if(delayUs>0 && delayUs<=100000)std::this_thread::sleep_for(std::chrono::microseconds(delayUs));size_t cap=0;uint8_t*b=AMediaCodec_getOutputBuffer(c,oi,&cap);obufs++;if(b&&info.size>0&&info.offset>=0&&(size_t)info.offset+(size_t)info.size<=cap){
    const char* probeEnv=getenv("T311_STABILITY_PROBE_US");
    int stabilityDelay=probeEnv?atoi(probeEnv):0;
    if(stabilityDelay>0 && stabilityDelay<=100000){
        std::vector<uint8_t> snapshot(b+info.offset,b+info.offset+info.size);
        std::this_thread::sleep_for(std::chrono::microseconds(stabilityDelay));
        size_t mismatch=0;
        for(size_t q=0;q<snapshot.size();++q) if(snapshot[q]!=b[info.offset+q])++mismatch;
        if(mismatch) printf("T311_C2_STABILITY_CHANGED out_index=%zd bytes=%zu firstval=%u latestval=%u\n",oi,mismatch,snapshot[0],b[info.offset]);
    }
    if(fwrite(b+info.offset,1,info.size,out)!=(size_t)info.size){printf("T311_C2_DEC_WRITE=FAIL at_buffer=%d\n",obufs);return 11;}obytes+=info.size;if(obufs%100==0)printf("T311_C2_DEC_PROGRESS=frames_%d bytes_%zu packets_%zu\n",obufs,obytes,pi);
}if(info.flags&AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM)eos=true;AMediaCodec_releaseOutputBuffer(c,oi,false);}else if(oi==AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED){AMediaFormat*of=AMediaCodec_getOutputFormat(c);printf("T311_C2_DEC_OUTPUT_FORMAT=%s\n",of?AMediaFormat_toString(of):"null");if(of)AMediaFormat_delete(of);}}
fclose(out);media_status_t sr=AMediaCodec_stop(c);AMediaCodec_delete(c);printf("T311_C2_DEC_STATS packets=%zu out_buffers=%d bytes=%zu eos=%d stop=%d\n",pi,obufs,obytes,eos,sr);bool ok=eos&&pi==pkts.size()&&obufs>0;printf("T311_C2_DEC_RESULT=%s size=%dx%d\n",ok?"PASS":"FAIL",w,h);return ok?0:20;}
