import android.media.MediaMetadataRetriever;
import android.graphics.Bitmap;
import android.util.Log;

public class MediaThumbProbe {
    public static void main(String[] args) {
        if (args.length < 1) throw new IllegalArgumentException("Need MP4 file");
        try (MediaMetadataRetriever m = new MediaMetadataRetriever()) {
            m.setDataSource(args[0]);
            System.out.println("META=" + m.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_WIDTH) +
                "x" + m.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_HEIGHT) +
                " duration=" + m.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION));
            for (int opt : new int[]{MediaMetadataRetriever.OPTION_CLOSEST_SYNC,
                    MediaMetadataRetriever.OPTION_PREVIOUS_SYNC}) {
                for (long us : new long[]{0L, 600000L, -1L}) {
                    try {
                        Bitmap b = m.getScaledFrameAtTime(us, opt, 160, 160);
                        System.out.println("FRAME option="+opt+" us="+us+" bitmap="+(b==null?"null":b.getWidth()+"x"+b.getHeight()));
                        if (b != null) b.recycle();
                    } catch (Throwable t) {
                        System.out.println("EXCEPTION "+opt+" "+us+" "+t);
                    }
                }
            }
        } catch (Throwable t) {
            t.printStackTrace();
            System.exit(3);
        }
    }
}
