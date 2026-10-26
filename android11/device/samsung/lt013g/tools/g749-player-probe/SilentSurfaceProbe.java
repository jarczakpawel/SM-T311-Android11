import android.media.MediaPlayer;
import android.graphics.SurfaceTexture;
import android.view.Surface;
import java.io.File;

public final class SilentSurfaceProbe {
    public static void main(String[] args) throws Exception {
        if (args.length != 1 || !new File(args[0]).isFile())
            throw new IllegalArgumentException("A local video-only MP4 required");
        // NEVER play sound: caller must use a verified video-only MP4.
        SurfaceTexture texture = new SurfaceTexture(1);
        texture.setDefaultBufferSize(640, 1024);
        Surface surface = new Surface(texture);
        MediaPlayer mp = new MediaPlayer();
        try {
            mp.setSurface(surface);
            mp.setDataSource(args[0]);
            mp.prepare();
            System.out.println("PREPARED="+mp.getVideoWidth()+"x"+mp.getVideoHeight());
            mp.start();
            Thread.sleep(900);
            System.out.println("RUNNING="+mp.isPlaying()+" pos="+mp.getCurrentPosition());
            mp.pause();
            System.out.println("PAUSE_OK");
        } finally {
            mp.release();
            surface.release();
            texture.release();
        }
    }
}
