import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaCodecList;
import android.media.MediaExtractor;
import android.media.MediaFormat;
import java.nio.ByteBuffer;
import java.io.File;

public final class VideoDecodeProbe {
    public static void main(String[] args) {
        MediaExtractor extractor = new MediaExtractor();
        MediaCodec decoder = null;
        try {
            if (args.length < 1 || !new File(args[0]).isFile()) throw new Exception("missing local video");
            extractor.setDataSource(args[0]);
            int t=-1;MediaFormat format=null;
            for (int i=0;i<extractor.getTrackCount();i++) {
                MediaFormat candidate=extractor.getTrackFormat(i);
                if (candidate.getString(MediaFormat.KEY_MIME).startsWith("video/")) {t=i;format=candidate;break;}
            }
            if (t<0) throw new Exception("no video");
            extractor.selectTrack(t);
            String mime=format.getString(MediaFormat.KEY_MIME);
            System.out.println("TRACK="+format.toString());
            decoder=args.length>1 ? MediaCodec.createByCodecName(args[1]) : MediaCodec.createDecoderByType(mime);
            System.out.println("DECODER="+decoder.getName());
            decoder.configure(format, null, null, 0);
            decoder.start();
            boolean fedEos=false;int input=0,outputs=0,loops=0;
            MediaCodec.BufferInfo info=new MediaCodec.BufferInfo();
            while (loops++<150 && outputs<30) {
                if (!fedEos) {
                    int slot=decoder.dequeueInputBuffer(30000);
                    if (slot>=0) {
                        ByteBuffer b=decoder.getInputBuffer(slot);
                        b.clear();
                        int size=extractor.readSampleData(b,0);
                        long pts=extractor.getSampleTime();
                        int flags=size<0?MediaCodec.BUFFER_FLAG_END_OF_STREAM:0;
                        decoder.queueInputBuffer(slot,0,Math.max(0,size),Math.max(0,pts),flags);
                        if(size<0)fedEos=true;else{input++;extractor.advance();}
                    }
                }
                int slot=decoder.dequeueOutputBuffer(info,30000);
                if(slot>=0){outputs++; decoder.releaseOutputBuffer(slot,false);if((info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM)!=0)break;}
                else if(slot==MediaCodec.INFO_OUTPUT_FORMAT_CHANGED)System.out.println("FORMAT="+decoder.getOutputFormat());
            }
            System.out.println("DECODE_RESULT inputs="+input+" outputs="+outputs+" loops="+loops);
            if(outputs==0)System.exit(4);
        }catch(Throwable t){ System.out.println("DECODE_EXCEPTION "+t);t.printStackTrace(System.out);System.exit(3);
        }finally{if(decoder!=null){try{decoder.stop();}catch(Throwable ignored){}try{decoder.release();}catch(Throwable ignored){}}extractor.release();}
    }
}
