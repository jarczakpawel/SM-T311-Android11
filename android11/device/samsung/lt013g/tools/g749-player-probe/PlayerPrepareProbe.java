import android.media.MediaPlayer;
import android.media.MediaMetadataRetriever;
import java.io.File;
public final class PlayerPrepareProbe {
  public static void main(String[] args) {
    if (args.length != 1 || !new File(args[0]).isFile()) {
      System.err.println("arg must be an existing MP4");
      System.exit(60);
    }
    MediaPlayer player = new MediaPlayer();
    try {
      player.setDataSource(args[0]);
      player.prepare(); // No Surface and NO start(): never plays video or audio.
      System.out.println("PREPARE_OK width="+player.getVideoWidth()+
        " height="+player.getVideoHeight()+" durationMs="+player.getDuration());
    } catch (Exception ex) {
      System.out.println("PREPARE_FAIL "+ex.getClass().getName()+" "+ex.getMessage());
      System.exit(1);
    } finally {
      player.release();
    }
  }
}
