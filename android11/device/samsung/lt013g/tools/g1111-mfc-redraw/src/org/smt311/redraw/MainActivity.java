package org.smt311.redraw;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.util.Log;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.WindowManager;

public final class MainActivity extends Activity implements SurfaceHolder.Callback {
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Paint paint = new Paint(Paint.FILTER_BITMAP_FLAG);
    private SurfaceHolder holder;
    private Bitmap texture;
    private boolean active;
    private boolean motion;
    private int frames;
    private long nextFrame;
    private long stopAt;
    private int intervalMs = 33;

    private final Runnable redraw = new Runnable() {
        @Override
        public void run() {
            if (!active || holder == null || texture == null) return;
            if (SystemClock.uptimeMillis() >= stopAt) {
                finish();
                return;
            }
            Canvas canvas = null;
            try {
                canvas = holder.lockHardwareCanvas();
                if (canvas != null) {
                    canvas.save();
                    canvas.scale(canvas.getWidth() / 640f, canvas.getHeight() / 1024f);
                    int offset = motion ? (frames * 13) % 640 : 0;
                    canvas.drawBitmap(texture, -offset, 0, paint);
                    canvas.drawBitmap(texture, 640 - offset, 0, paint);
                    canvas.restore();
                    frames++;
                    if (frames % 10 == 0) {
                        Log.i("G1112", "mode=" + (motion ? "motion" : "static") + " interval_ms=" + intervalMs + " submitted=" + frames);
                    }
                }
            } catch (RuntimeException e) {
                active = false;
                Log.e("G1111", "draw failed", e);
            } finally {
                if (canvas != null) holder.unlockCanvasAndPost(canvas);
            }
            if (active) {
                nextFrame += intervalMs;
                long now = SystemClock.uptimeMillis();
                if (nextFrame <= now) nextFrame = now + intervalMs;
                handler.postAtTime(this, nextFrame);
            }
        }
    };

    @Override
    public void onCreate(Bundle state) {
        super.onCreate(state);
        String mode = getIntent().getStringExtra("mode");
        if (!"static".equals(mode) && !"motion".equals(mode)) {
            finish();
            return;
        }
        motion = "motion".equals(mode);
        intervalMs = getIntent().getIntExtra("interval_ms", 33);
        if (intervalMs != 33 && intervalMs != 66 && intervalMs != 133) {
            finish();
            return;
        }
        stopAt = SystemClock.uptimeMillis() + 16000;
        getWindow().setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN,
                WindowManager.LayoutParams.FLAG_FULLSCREEN);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        WindowManager.LayoutParams attributes = getWindow().getAttributes();
        attributes.screenBrightness = 0.03f;
        getWindow().setAttributes(attributes);
        int[] pixels = new int[640 * 1024];
        int seed = 0x31ab0424;
        for (int i = 0; i < pixels.length; i++) {
            seed = seed * 1664525 + 1013904223;
            int channel = 16 + ((seed >>> 24) & 63);
            pixels[i] = 0xff000000 | channel << 16 | channel << 8 | channel;
        }
        texture = Bitmap.createBitmap(pixels, 640, 1024, Bitmap.Config.ARGB_8888);
        SurfaceView surface = new SurfaceView(this);
        setContentView(surface);
        holder = surface.getHolder();
        holder.addCallback(this);
        Log.i("G1112", "created mode=" + mode + " width=640 height=1024 interval_ms=" + intervalMs);
    }

    @Override
    public void surfaceCreated(SurfaceHolder surfaceHolder) {
        if (active) return;
        active = true;
        frames = 0;
        nextFrame = SystemClock.uptimeMillis();
        handler.post(redraw);
    }

    @Override
    public void surfaceChanged(SurfaceHolder surfaceHolder, int format, int width, int height) {
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder surfaceHolder) {
        active = false;
        handler.removeCallbacks(redraw);
    }

    @Override
    protected void onPause() {
        active = false;
        handler.removeCallbacks(redraw);
        super.onPause();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (holder != null && holder.getSurface().isValid() && texture != null && !active) {
            active = true;
            nextFrame = SystemClock.uptimeMillis();
            handler.post(redraw);
        }
    }

    @Override
    protected void onDestroy() {
        active = false;
        handler.removeCallbacks(redraw);
        if (texture != null) texture.recycle();
        super.onDestroy();
    }
}
