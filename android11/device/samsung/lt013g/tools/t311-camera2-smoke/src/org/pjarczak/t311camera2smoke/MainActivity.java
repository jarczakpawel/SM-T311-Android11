package org.pjarczak.t311camera2smoke;

import android.Manifest;
import android.app.Activity;
import android.content.pm.PackageManager;
import android.graphics.ImageFormat;
import android.hardware.camera2.CameraAccessException;
import android.hardware.camera2.CameraCaptureSession;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraDevice;
import android.hardware.camera2.CameraManager;
import android.hardware.camera2.CaptureRequest;
import android.hardware.camera2.params.StreamConfigurationMap;
import android.media.Image;
import android.media.ImageReader;
import android.media.MediaRecorder;
import android.media.MediaExtractor;
import android.media.MediaFormat;
import android.media.MediaCodecInfo;
import android.media.MediaCodecList;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.util.Log;
import android.util.Size;
import android.view.Surface;
import android.view.WindowManager;
import android.widget.TextView;

import java.io.File;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.AtomicReference;

public final class MainActivity extends Activity {
    private static final String TAG = "T311CAM2";
    private static final int REQ_CAMERA = 311;
    private final AtomicBoolean started = new AtomicBoolean(false);
    private final AtomicInteger passCount = new AtomicInteger();
    private final AtomicInteger failCount = new AtomicInteger();
    private volatile boolean resumed;

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        setShowWhenLocked(true);
        setTurnScreenOn(true);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        TextView v = new TextView(this);
        v.setText("SM-T311 Camera2 capability audit is running...");
        v.setTextSize(20.0f);
        v.setPadding(32, 32, 32, 32);
        setContentView(v);
        if (checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED ||
                (getIntent().getBooleanExtra("av_audio", false) &&
                 checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED))
            requestPermissions(getIntent().getBooleanExtra("av_audio", false) ?
                    new String[]{Manifest.permission.CAMERA, Manifest.permission.RECORD_AUDIO} :
                    new String[]{Manifest.permission.CAMERA}, REQ_CAMERA);
    }

    @Override
    protected void onResume() {
        super.onResume();
        resumed = true;
        maybeStart();
    }

    @Override
    protected void onPause() {
        resumed = false;
        super.onPause();
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grants) {
        super.onRequestPermissionsResult(requestCode, permissions, grants);
        if (requestCode == REQ_CAMERA) {
            if (checkSelfPermission(Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED &&
                    (!getIntent().getBooleanExtra("av_audio", false) ||
                    checkSelfPermission(Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED)) {
                maybeStart();
            } else {
                Log.e(TAG, "T311_CAM2_AUDIT_FINAL=FAIL reason=CAMERA_OR_MIC_PERMISSION");
                finish();
            }
        }
    }

    private void maybeStart() {
        if (!resumed) return;
        if (checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) return;
        if (getIntent().getBooleanExtra("av_audio", false) &&
                checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) return;
        if (!started.compareAndSet(false, true)) return;
        Log.e(TAG, "T311_CAM2_AUDIT_FOREGROUND_READY");
        new Handler(getMainLooper()).postDelayed(this::startAudit, 750);
    }

    private void startAudit() {
        new Thread(() -> {
            try {
                dumpAvcEncoders();
                CameraManager mgr = getSystemService(CameraManager.class);
                String[] ids = mgr.getCameraIdList();
                Log.e(TAG, "T311_CAM2_AUDIT_IDS=" + Arrays.toString(ids));
                if (!Arrays.asList(ids).contains("0") || !Arrays.asList(ids).contains("1"))
                    throw new IllegalStateException("camera IDs 0 and 1 not both present");

                dumpCharacteristics(mgr, "0", "REAR");
                dumpCharacteristics(mgr, "1", "FRONT");

                // G593: isolate the H.264 V4L2 recorder path for each camera.
                // The full smoke test follows video errors with switching cases;
                // those can be polluted by CameraService UID/policy callbacks.
                // Run one explicitly selected video session and return. No mic.
                if (getIntent().getBooleanExtra("video_only", false)) {
                    String role = getIntent().getStringExtra("video_role");
                    if ("FRONT".equals(role)) {
                        runCase("VIDEO_ONLY", "FRONT", "1", "640x480@25",
                                () -> testVideo(mgr, "1", "FRONT", new Size(640, 480)));
                    } else if ("REAR".equals(role)) {
                        runCase("VIDEO_ONLY", "REAR", "0", "640x480@30",
                                () -> testVideo(mgr, "0", "REAR", new Size(640, 480)));
                    } else {
                        throw new IllegalArgumentException("video_only requires video_role=FRONT or REAR");
                    }
                    Log.e(TAG, "T311_CAM2_AUDIT_FINAL=" +
                            (failCount.get() == 0 ? "PASS" : "FAIL") +
                            " pass=" + passCount.get() + " fail=" + failCount.get());
                    return;
                }

                if (getIntent().getBooleanExtra("smoke_only", false)) {
                    runCase("YUV", "REAR", "0", "640x480", () -> testImageStream(mgr, "0", "REAR", ImageFormat.YUV_420_888, new Size(640, 480)));
                    runCase("YUV", "FRONT", "1", "640x480", () -> testImageStream(mgr, "1", "FRONT", ImageFormat.YUV_420_888, new Size(640, 480)));
                    runCase("JPEG", "REAR", "0", "640x480", () -> testImageStream(mgr, "0", "REAR", ImageFormat.JPEG, new Size(640, 480)));
                    runCase("JPEG", "FRONT", "1", "640x480", () -> testImageStream(mgr, "1", "FRONT", ImageFormat.JPEG, new Size(640, 480)));
                } else {
                    auditFormat(mgr, "0", "REAR", ImageFormat.YUV_420_888, "YUV");
                    auditFormat(mgr, "0", "REAR", ImageFormat.JPEG, "JPEG");
                    auditFormat(mgr, "0", "REAR", ImageFormat.PRIVATE, "PRIVATE");
                    auditFormat(mgr, "1", "FRONT", ImageFormat.YUV_420_888, "YUV");
                    auditFormat(mgr, "1", "FRONT", ImageFormat.JPEG, "JPEG");
                    auditFormat(mgr, "1", "FRONT", ImageFormat.PRIVATE, "PRIVATE");
                }

                runCase("COMBO", "REAR", "0", "640x480+MAXJPEG",
                        () -> testPreviewJpegCombo(mgr, "0", "REAR"));
                runCase("COMBO", "FRONT", "1", "640x480+MAXJPEG",
                        () -> testPreviewJpegCombo(mgr, "1", "FRONT"));

                runCase("VIDEO", "REAR", "0", "640x480@30",
                        () -> testVideo(mgr, "0", "REAR", new Size(640, 480)));
                runCase("VIDEO", "FRONT", "1", "640x480@25",
                        () -> testVideo(mgr, "1", "FRONT", new Size(640, 480)));

                runCase("SWITCH", "REAR", "0", "640x480",
                        () -> testImageStream(mgr, "0", "REAR-SWITCH", ImageFormat.YUV_420_888,
                                new Size(640, 480)));
                runCase("SWITCH", "FRONT", "1", "640x480",
                        () -> testImageStream(mgr, "1", "FRONT-SWITCH", ImageFormat.YUV_420_888,
                                new Size(640, 480)));
                runCase("SWITCH", "REAR", "0", "640x480",
                        () -> testImageStream(mgr, "0", "REAR-SWITCH2", ImageFormat.YUV_420_888,
                                new Size(640, 480)));

                String result = failCount.get() == 0 ? "PASS" : "FAIL";
                Log.e(TAG, "T311_CAM2_AUDIT_FINAL=" + result +
                        " pass=" + passCount.get() + " fail=" + failCount.get());
            } catch (Throwable t) {
                Log.e(TAG, "T311_CAM2_AUDIT_FINAL=FAIL exception=" +
                        t.getClass().getName() + " message=" + t.getMessage(), t);
            } finally {
                runOnUiThread(this::finish);
            }
        }, "T311Camera2Audit").start();
    }

    private void dumpAvcEncoders() {
        int count = 0;
        try {
            MediaCodecInfo[] infos = new MediaCodecList(MediaCodecList.ALL_CODECS).getCodecInfos();
            for (MediaCodecInfo info : infos) {
                if (!info.isEncoder()) continue;
                for (String type : info.getSupportedTypes()) {
                    if (!"video/avc".equalsIgnoreCase(type)) continue;
                    count++;
                    Log.e(TAG, "T311_CAM2_AVC_ENCODER name=" + info.getName() +
                            " hw=" + info.isHardwareAccelerated() +
                            " sw=" + info.isSoftwareOnly() +
                            " vendor=" + info.isVendor());
                }
            }
        } catch (Throwable t) {
            Log.e(TAG, "T311_CAM2_AVC_ENCODER_ENUM_FAIL=" + t.getClass().getName() +
                    ":" + t.getMessage(), t);
        }
        Log.e(TAG, "T311_CAM2_AVC_ENCODER_COUNT=" + count);
    }

    private void dumpCharacteristics(CameraManager mgr, String id, String role) throws Exception {
        CameraCharacteristics c = mgr.getCameraCharacteristics(id);
        StreamConfigurationMap map = c.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP);
        Log.e(TAG, "T311_CAM2_AUDIT_CHARS role=" + role + " id=" + id +
                " facing=" + c.get(CameraCharacteristics.LENS_FACING) +
                " orientation=" + c.get(CameraCharacteristics.SENSOR_ORIENTATION) +
                " hw=" + c.get(CameraCharacteristics.INFO_SUPPORTED_HARDWARE_LEVEL) +
                " flash=" + c.get(CameraCharacteristics.FLASH_INFO_AVAILABLE) +
                " active=" + rect(c.get(CameraCharacteristics.SENSOR_INFO_ACTIVE_ARRAY_SIZE)) +
                " pixel=" + size(c.get(CameraCharacteristics.SENSOR_INFO_PIXEL_ARRAY_SIZE)) +
                " zoom=" + c.get(CameraCharacteristics.SCALER_AVAILABLE_MAX_DIGITAL_ZOOM) +
                " af=" + ints(c.get(CameraCharacteristics.CONTROL_AF_AVAILABLE_MODES)) +
                " awb=" + ints(c.get(CameraCharacteristics.CONTROL_AWB_AVAILABLE_MODES)) +
                " effects=" + ints(c.get(CameraCharacteristics.CONTROL_AVAILABLE_EFFECTS)) +
                " scenes=" + ints(c.get(CameraCharacteristics.CONTROL_AVAILABLE_SCENE_MODES)) +
                " fps=" + Arrays.toString(c.get(CameraCharacteristics.CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES)));
        if (map != null) {
            Log.e(TAG, "T311_CAM2_AUDIT_SIZES role=" + role + " format=YUV sizes=" +
                    Arrays.toString(sorted(map.getOutputSizes(ImageFormat.YUV_420_888))));
            Log.e(TAG, "T311_CAM2_AUDIT_SIZES role=" + role + " format=JPEG sizes=" +
                    Arrays.toString(sorted(map.getOutputSizes(ImageFormat.JPEG))));
            Log.e(TAG, "T311_CAM2_AUDIT_SIZES role=" + role + " format=PRIVATE sizes=" +
                    Arrays.toString(sorted(map.getOutputSizes(ImageFormat.PRIVATE))));
        }
    }

    private void auditFormat(CameraManager mgr, String id, String role, int format, String type)
            throws Exception {
        CameraCharacteristics c = mgr.getCameraCharacteristics(id);
        StreamConfigurationMap map = c.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP);
        if (map == null) throw new IllegalStateException(role + " no stream map");
        Size[] sizes = sorted(map.getOutputSizes(format));
        if (sizes == null || sizes.length == 0) {
            runCase(type, role, id, "NO_SIZES", () -> {
                throw new IllegalStateException("no advertised " + type + " sizes");
            });
            return;
        }
        for (Size s : sizes) {
            runCase(type, role, id, s.toString(), () -> testImageStream(mgr, id, role, format, s));
            Thread.sleep(180);
        }
    }

    private void runCase(String type, String role, String id, String detail, ThrowingRunnable r) {
        long start = System.nanoTime();
        try {
            r.run();
            long ms = TimeUnit.NANOSECONDS.toMillis(System.nanoTime() - start);
            passCount.incrementAndGet();
            Log.e(TAG, "T311_CAM2_AUDIT_CASE_PASS type=" + type + " role=" + role +
                    " id=" + id + " detail=" + detail + " elapsedMs=" + ms);
        } catch (Throwable t) {
            long ms = TimeUnit.NANOSECONDS.toMillis(System.nanoTime() - start);
            failCount.incrementAndGet();
            Log.e(TAG, "T311_CAM2_AUDIT_CASE_FAIL type=" + type + " role=" + role +
                    " id=" + id + " detail=" + detail + " elapsedMs=" + ms +
                    " exception=" + t.getClass().getName() + " message=" + t.getMessage(), t);
        }
    }

    private void testImageStream(CameraManager mgr, String id, String role, int format, Size size)
            throws Exception {
        CamCtx ctx = openCamera(mgr, id, role + "-" + format + "-" + size);
        ImageReader reader = null;
        CameraCaptureSession session = null;
        try {
            AtomicReference<String> frameError = new AtomicReference<>();
            AtomicLong frameMetric = new AtomicLong(-1);
            CountDownLatch frameLatch = new CountDownLatch(1);
            AtomicBoolean accepted = new AtomicBoolean(false);
            AtomicInteger seen = new AtomicInteger();

            reader = ImageReader.newInstance(size.getWidth(), size.getHeight(), format, 4);
            reader.setOnImageAvailableListener(r -> {
                Image image = null;
                boolean signal = false;
                try {
                    image = r.acquireLatestImage();
                    if (image == null) return;
                    if (format != ImageFormat.JPEG && seen.incrementAndGet() < 4) return;
                    if (!accepted.compareAndSet(false, true)) return;
                    if (format == ImageFormat.YUV_420_888) {
                        Image.Plane[] planes = image.getPlanes();
                        if (planes.length < 2) throw new IllegalStateException("YUV planes=" + planes.length);
                        ByteBuffer y = planes[0].getBuffer().duplicate();
                        int rem = y.remaining();
                        int step = Math.max(1, rem / 2048);
                        long sum = 0;
                        int min = 255, max = 0, samples = 0;
                        for (int p = y.position(); p < y.limit(); p += step) {
                            int v = y.get(p) & 0xff;
                            sum += v;
                            min = Math.min(min, v);
                            max = Math.max(max, v);
                            samples++;
                        }
                        if (samples == 0) throw new IllegalStateException("empty Y plane");
                        frameMetric.set(sum);
                        Log.e(TAG, "T311_CAM2_AUDIT_FRAME type=YUV role=" + role + " size=" + size +
                                " planes=" + planes.length + " yRow=" + planes[0].getRowStride() +
                                " yPix=" + planes[0].getPixelStride() + " uvRow=" +
                                planes[1].getRowStride() + " uvPix=" + planes[1].getPixelStride() +
                                " min=" + min + " max=" + max + " mean=" + (sum / samples) +
                                " ts=" + image.getTimestamp());
                    } else if (format == ImageFormat.JPEG) {
                        Image.Plane[] planes = image.getPlanes();
                        if (planes.length != 1) throw new IllegalStateException("JPEG planes=" + planes.length);
                        ByteBuffer b = planes[0].getBuffer().duplicate();
                        int n = b.remaining();
                        if (n < 4) throw new IllegalStateException("JPEG too small=" + n);
                        int pos = b.position();
                        boolean soi = (b.get(pos) & 0xff) == 0xff && (b.get(pos + 1) & 0xff) == 0xd8;
                        if (!soi) throw new IllegalStateException("invalid JPEG SOI bytes=" + n);
                        frameMetric.set(n);
                        Log.e(TAG, "T311_CAM2_AUDIT_FRAME type=JPEG role=" + role + " size=" + size +
                                " bytes=" + n + " ts=" + image.getTimestamp());
                    } else if (format == ImageFormat.PRIVATE) {
                        frameMetric.set(1);
                        Log.e(TAG, "T311_CAM2_AUDIT_FRAME type=PRIVATE role=" + role + " size=" + size +
                                " planes=" + image.getPlanes().length + " ts=" + image.getTimestamp());
                    } else {
                        throw new IllegalStateException("unsupported test format=" + format);
                    }
                    signal = true;
                } catch (Throwable t) {
                    frameError.set(t.getClass().getName() + ":" + t.getMessage());
                    signal = true;
                } finally {
                    if (image != null) image.close();
                    if (signal) frameLatch.countDown();
                }
            }, ctx.handler);

            session = createSession(ctx.camera, Arrays.asList(reader.getSurface()), ctx.handler);
            CaptureRequest.Builder req = ctx.camera.createCaptureRequest(
                    format == ImageFormat.JPEG ? CameraDevice.TEMPLATE_STILL_CAPTURE :
                            CameraDevice.TEMPLATE_PREVIEW);
            req.addTarget(reader.getSurface());
            req.set(CaptureRequest.CONTROL_MODE, CaptureRequest.CONTROL_MODE_AUTO);
            if (format == ImageFormat.JPEG)
                session.capture(req.build(), null, ctx.handler);
            else
                session.setRepeatingRequest(req.build(), null, ctx.handler);

            if (!frameLatch.await(12, TimeUnit.SECONDS))
                throw new IllegalStateException("frame timeout");
            if (frameError.get() != null)
                throw new IllegalStateException(frameError.get());
            if (frameMetric.get() <= 0)
                throw new IllegalStateException("invalid frame metric=" + frameMetric.get());

            try { session.stopRepeating(); } catch (Throwable ignored) {}
            try { session.abortCaptures(); } catch (Throwable ignored) {}
            reader.setOnImageAvailableListener(null, null);
            drainHandler(ctx.handler);
        } finally {
            if (session != null) session.close();
            if (reader != null) reader.close();
            closeCamera(ctx);
        }
    }

    private void testPreviewJpegCombo(CameraManager mgr, String id, String role) throws Exception {
        CameraCharacteristics chars = mgr.getCameraCharacteristics(id);
        StreamConfigurationMap map = chars.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP);
        Size previewSize = requireSize(map.getOutputSizes(ImageFormat.PRIVATE), 640, 480);
        Size jpegSize = largest(map.getOutputSizes(ImageFormat.JPEG));
        CamCtx ctx = openCamera(mgr, id, role + "-COMBO");
        ImageReader preview = null, jpeg = null;
        CameraCaptureSession session = null;
        try {
            CountDownLatch previewLatch = new CountDownLatch(1);
            CountDownLatch jpegLatch = new CountDownLatch(1);
            AtomicReference<String> err = new AtomicReference<>();
            AtomicBoolean gotPreview = new AtomicBoolean(false);
            AtomicBoolean gotJpeg = new AtomicBoolean(false);

            preview = ImageReader.newInstance(previewSize.getWidth(), previewSize.getHeight(),
                    ImageFormat.PRIVATE, 4);
            jpeg = ImageReader.newInstance(jpegSize.getWidth(), jpegSize.getHeight(), ImageFormat.JPEG, 2);
            preview.setOnImageAvailableListener(r -> consumeOpaque(r, gotPreview, previewLatch, err), ctx.handler);
            jpeg.setOnImageAvailableListener(r -> consumeJpeg(r, gotJpeg, jpegLatch, err), ctx.handler);

            session = createSession(ctx.camera,
                    Arrays.asList(preview.getSurface(), jpeg.getSurface()), ctx.handler);
            CaptureRequest.Builder p = ctx.camera.createCaptureRequest(CameraDevice.TEMPLATE_PREVIEW);
            p.addTarget(preview.getSurface());
            p.set(CaptureRequest.CONTROL_MODE, CaptureRequest.CONTROL_MODE_AUTO);
            session.setRepeatingRequest(p.build(), null, ctx.handler);
            if (!previewLatch.await(12, TimeUnit.SECONDS))
                throw new IllegalStateException("combo preview timeout");
            if (err.get() != null) throw new IllegalStateException(err.get());

            CaptureRequest.Builder j = ctx.camera.createCaptureRequest(CameraDevice.TEMPLATE_STILL_CAPTURE);
            j.addTarget(jpeg.getSurface());
            j.set(CaptureRequest.CONTROL_MODE, CaptureRequest.CONTROL_MODE_AUTO);
            session.capture(j.build(), null, ctx.handler);
            if (!jpegLatch.await(15, TimeUnit.SECONDS))
                throw new IllegalStateException("combo jpeg timeout");
            if (err.get() != null) throw new IllegalStateException(err.get());
            Log.e(TAG, "T311_CAM2_AUDIT_COMBO role=" + role + " preview=" + previewSize +
                    " jpeg=" + jpegSize);

            try { session.stopRepeating(); } catch (Throwable ignored) {}
            try { session.abortCaptures(); } catch (Throwable ignored) {}
            preview.setOnImageAvailableListener(null, null);
            jpeg.setOnImageAvailableListener(null, null);
            drainHandler(ctx.handler);
        } finally {
            if (session != null) session.close();
            if (preview != null) preview.close();
            if (jpeg != null) jpeg.close();
            closeCamera(ctx);
        }
    }

    private void testVideo(CameraManager mgr, String id, String role, Size size) throws Exception {
        CamCtx ctx = openCamera(mgr, id, role + "-VIDEO");
        MediaRecorder recorder = new MediaRecorder();
        CameraCaptureSession session = null;
        File out = new File(getCacheDir(), "t311-" + role + "-" + System.nanoTime() + ".mp4");
        try {
            boolean audioEnabled = getIntent().getBooleanExtra("av_audio", false);
            recorder.setVideoSource(MediaRecorder.VideoSource.SURFACE);
            if (audioEnabled) recorder.setAudioSource(MediaRecorder.AudioSource.MIC);
            recorder.setOutputFormat(MediaRecorder.OutputFormat.MPEG_4);
            recorder.setVideoEncoder(MediaRecorder.VideoEncoder.H264);
            if (audioEnabled) {
                recorder.setAudioEncoder(MediaRecorder.AudioEncoder.AAC);
                recorder.setAudioSamplingRate(48000);
                recorder.setAudioEncodingBitRate(128000);
            }
            recorder.setVideoSize(size.getWidth(), size.getHeight());
            recorder.setVideoFrameRate("FRONT".equals(role) ? 25 : 30);
            recorder.setVideoEncodingBitRate(2_000_000);
            recorder.setOutputFile(out.getAbsolutePath());
            recorder.prepare();
            Surface surface = recorder.getSurface();
            session = createSession(ctx.camera, Arrays.asList(surface), ctx.handler);
            CaptureRequest.Builder req = ctx.camera.createCaptureRequest(CameraDevice.TEMPLATE_RECORD);
            req.addTarget(surface);
            req.set(CaptureRequest.CONTROL_MODE, CaptureRequest.CONTROL_MODE_AUTO);
            session.setRepeatingRequest(req.build(), null, ctx.handler);
            Thread.sleep(500);
            recorder.start();
            Thread.sleep(2200);
            recorder.stop();
            long bytes = out.length();
            if (bytes < 4096) throw new IllegalStateException("video too small=" + bytes);
            // G1143: MP4 >4KiB does not prove a valid multi-frame H.264
            // recording. Validate actual demuxed track packets and PTS
            // BEFORE declaring the camera test successful. This does NOT
            // enable audio, decode frames or preserve private media files.
            verifyAvcSamples(out, size, role);
            if (audioEnabled) {
                MediaExtractor extractor = new MediaExtractor();
                try {
                    extractor.setDataSource(out.getAbsolutePath());
                    int audioTrack = -1, videoTrack = -1;
                    for (int i = 0; i < extractor.getTrackCount(); i++) {
                        MediaFormat format = extractor.getTrackFormat(i);
                        String mime = format.getString(MediaFormat.KEY_MIME);
                        if ("video/avc".equals(mime)) videoTrack = i;
                        if ("audio/mp4a-latm".equals(mime)) {
                            int rate = format.getInteger(MediaFormat.KEY_SAMPLE_RATE);
                            if (rate != 48000) throw new IllegalStateException("AAC rate=" + rate);
                            audioTrack = i;
                        }
                    }
                    if (audioTrack < 0 || videoTrack < 0)
                        throw new IllegalStateException("MP4 tracks audio=" + audioTrack + " video=" + videoTrack);
                    extractor.selectTrack(audioTrack);
                    extractor.selectTrack(videoTrack);
                    long[] firstPts = {-1, -1}, lastPts = {-1, -1};
                    int[] samples = {0, 0};
                    for (int i = 0; i < 2000; i++) {
                        int track = extractor.getSampleTrackIndex();
                        if (track < 0) break;
                        long pts = extractor.getSampleTime();
                        int k = track == videoTrack ? 0 : track == audioTrack ? 1 : -1;
                        if (k >= 0 && pts >= 0) {
                            if (firstPts[k] < 0) firstPts[k] = pts;
                            lastPts[k] = pts;
                            samples[k]++;
                        }
                        if (!extractor.advance()) break;
                    }
                    if (samples[0] < 2 || samples[1] < 2)
                        throw new IllegalStateException("MP4 samples video=" + samples[0] + " audio=" + samples[1]);
                    Log.e(TAG, "T311_CAM2_AV_AUDIO role=" + role + " aacRate=48000 video=H264" +
                            " videoSamples=" + samples[0] + " audioSamples=" + samples[1] +
                            " videoPtsUs=" + firstPts[0] + ".." + lastPts[0] +
                            " audioPtsUs=" + firstPts[1] + ".." + lastPts[1]);
                } finally {
                    extractor.release();
                }
            }
            Log.e(TAG, "T311_CAM2_AUDIT_VIDEO role=" + role + " size=" + size + " bytes=" + bytes);
            try { session.stopRepeating(); } catch (Throwable ignored) {}
            try { session.abortCaptures(); } catch (Throwable ignored) {}
        } finally {
            if (session != null) session.close();
            try { recorder.reset(); } catch (Throwable ignored) {}
            recorder.release();
            closeCamera(ctx);
            // Opt-in local-only QA clip for strict FFmpeg decoder diagnosis.
            // Never retain by default; never retain clips containing audio.
            boolean retainQa = getIntent().getBooleanExtra("retain_private_mp4_for_qa", false)
                    && getIntent().getBooleanExtra("video_only", false)
                    && !getIntent().getBooleanExtra("av_audio", false);
            if (out.exists() && retainQa) {
                Log.e(TAG, "T311_CAM2_QA_PRIVATE_VIDEO_PATH role=" + role +
                        " path=" + out.getAbsolutePath() +
                        " bytes=" + out.length() + " REMOVE_AFTER_PRIVATE_DECODER_CHECK");
            } else if (out.exists()) {
                out.delete();
            }
        }
    }

    // G1143: narrow validation of actual MP4 H.264 samples. No video
    // frame/PCM payloads leave the app cache; testVideo() always deletes MP4.
    // This is a demux/PTS consistency gate, NOT a strict decoder: malformed
    // H.264 entropy can still pass and must be analyzed with a decoder.
    private void verifyAvcSamples(File mp4, Size expectedSize, String role) throws Exception {
        MediaExtractor ex = new MediaExtractor();
        try {
            ex.setDataSource(mp4.getAbsolutePath());
            int trackIndex = -1;
            int width = -1, height = -1;
            for (int i = 0; i < ex.getTrackCount(); i++) {
                MediaFormat f = ex.getTrackFormat(i);
                String mime = f.getString(MediaFormat.KEY_MIME);
                if (!"video/avc".equalsIgnoreCase(mime)) continue;
                if (trackIndex >= 0)
                    throw new IllegalStateException("more than one AVC track");
                trackIndex = i;
                width = f.getInteger(MediaFormat.KEY_WIDTH);
                height = f.getInteger(MediaFormat.KEY_HEIGHT);
            }
            if (trackIndex < 0)
                throw new IllegalStateException(role + " MP4 contains no video/avc track");
            if (width != expectedSize.getWidth() || height != expectedSize.getHeight())
                throw new IllegalStateException(role + " AVC dimensions " +
                        width + "x" + height + " != expected " + expectedSize);
            ex.selectTrack(trackIndex);
            int frames = 0, syncFrames = 0;
            long firstPts = -1, lastPts = -1, sampleBytes = 0;
            for (int n = 0; n < 2000; n++) {
                if (ex.getSampleTrackIndex() < 0) break;
                if (ex.getSampleTrackIndex() != trackIndex)
                    throw new IllegalStateException(role + " unexpected selected sample track");
                long pts = ex.getSampleTime();
                long size = ex.getSampleSize();
                if (pts < 0 || size <= 0)
                    throw new IllegalStateException(role + " invalid AVC pts=" + pts + " size=" + size);
                if (firstPts < 0) firstPts = pts;
                if (lastPts > pts)
                    throw new IllegalStateException(role + " non-monotonic AVC PTS");
                lastPts = pts;
                sampleBytes += size;
                if ((ex.getSampleFlags() & MediaExtractor.SAMPLE_FLAG_SYNC) != 0) syncFrames++;
                frames++;
                if (!ex.advance()) break;
            }
            long spanUs = firstPts < 0 ? 0 : lastPts - firstPts;
            Log.e(TAG, "T311_CAM2_AUDIT_AVC_SAMPLES role=" + role +
                    " frames=" + frames + " sync=" + syncFrames +
                    " spanUs=" + spanUs + " mediaBytes=" + sampleBytes +
                    " fileBytes=" + mp4.length() +
                    " videoOnly=" + !getIntent().getBooleanExtra("av_audio", false));
            // Dark/static video may legitimately contain only ~30 bytes per
            // P frame. The old absolute 2048-byte cutoff FALSE-FAILED
            // physical rear 67-sample, 2.199s MP4 (1979B coded data).
            // Use a small relative lower bound; a strict H264 decoder is
            // required separately to determine entropy correctness.
            if (frames < 20 || syncFrames < 1 || spanUs < 1_000_000 ||
                    sampleBytes < frames * 8L)
                throw new IllegalStateException(role + " incomplete AVC recording frames=" +
                        frames + " sync=" + syncFrames + " spanUs=" + spanUs +
                        " mediaBytes=" + sampleBytes);
        } finally {
            ex.release();
        }
    }

    private static void consumeOpaque(ImageReader r, AtomicBoolean accepted,
            CountDownLatch latch, AtomicReference<String> err) {
        Image image = null;
        boolean signal = false;
        try {
            image = r.acquireLatestImage();
            if (image == null) return;
            if (!accepted.compareAndSet(false, true)) return;
            signal = true;
        } catch (Throwable t) {
            err.set(t.getClass().getName() + ":" + t.getMessage());
            signal = true;
        } finally {
            if (image != null) image.close();
            if (signal) latch.countDown();
        }
    }

    private static void consumeJpeg(ImageReader r, AtomicBoolean accepted,
            CountDownLatch latch, AtomicReference<String> err) {
        Image image = null;
        boolean signal = false;
        try {
            image = r.acquireLatestImage();
            if (image == null) return;
            if (!accepted.compareAndSet(false, true)) return;
            ByteBuffer b = image.getPlanes()[0].getBuffer().duplicate();
            int n = b.remaining();
            int p = b.position();
            if (n < 4 || (b.get(p) & 0xff) != 0xff || (b.get(p + 1) & 0xff) != 0xd8)
                throw new IllegalStateException("invalid combo JPEG bytes=" + n);
            signal = true;
        } catch (Throwable t) {
            err.set(t.getClass().getName() + ":" + t.getMessage());
            signal = true;
        } finally {
            if (image != null) image.close();
            if (signal) latch.countDown();
        }
    }

    private static CamCtx openCamera(CameraManager mgr, String id, String label) throws Exception {
        HandlerThread thread = new HandlerThread("T311-" + label);
        thread.start();
        Handler handler = new Handler(thread.getLooper());
        AtomicReference<CameraDevice> ref = new AtomicReference<>();
        AtomicReference<String> err = new AtomicReference<>();
        CountDownLatch latch = new CountDownLatch(1);
        mgr.openCamera(id, new CameraDevice.StateCallback() {
            @Override public void onOpened(CameraDevice camera) {
                ref.set(camera);
                latch.countDown();
            }
            @Override public void onDisconnected(CameraDevice camera) {
                err.set("DISCONNECTED");
                camera.close();
                latch.countDown();
            }
            @Override public void onError(CameraDevice camera, int error) {
                err.set("ERROR_" + error);
                camera.close();
                latch.countDown();
            }
        }, handler);
        if (!latch.await(10, TimeUnit.SECONDS)) {
            thread.quitSafely();
            throw new IllegalStateException("open timeout " + label);
        }
        if (err.get() != null || ref.get() == null) {
            thread.quitSafely();
            throw new IllegalStateException("open failed " + label + " " + err.get());
        }
        return new CamCtx(ref.get(), thread, handler);
    }

    private static CameraCaptureSession createSession(CameraDevice camera, List<Surface> surfaces,
            Handler handler) throws Exception {
        AtomicReference<CameraCaptureSession> ref = new AtomicReference<>();
        AtomicReference<String> err = new AtomicReference<>();
        CountDownLatch latch = new CountDownLatch(1);
        camera.createCaptureSession(surfaces, new CameraCaptureSession.StateCallback() {
            @Override public void onConfigured(CameraCaptureSession session) {
                ref.set(session);
                latch.countDown();
            }
            @Override public void onConfigureFailed(CameraCaptureSession session) {
                err.set("CONFIGURE_FAILED");
                latch.countDown();
            }
        }, handler);
        if (!latch.await(12, TimeUnit.SECONDS)) throw new IllegalStateException("session timeout");
        if (err.get() != null || ref.get() == null)
            throw new IllegalStateException("session failed " + err.get());
        return ref.get();
    }

    private static void closeCamera(CamCtx ctx) throws Exception {
        ctx.camera.close();
        drainHandler(ctx.handler);
        ctx.thread.quitSafely();
        ctx.thread.join(3000);
        Thread.sleep(120);
    }

    private static void drainHandler(Handler h) throws InterruptedException {
        CountDownLatch l = new CountDownLatch(1);
        h.post(l::countDown);
        l.await(2, TimeUnit.SECONDS);
    }

    private static Size[] sorted(Size[] in) {
        if (in == null) return null;
        Size[] out = in.clone();
        Arrays.sort(out, Comparator.comparingLong(s -> (long) s.getWidth() * s.getHeight()));
        return out;
    }

    private static Size requireSize(Size[] sizes, int w, int h) {
        if (sizes != null) for (Size s : sizes)
            if (s.getWidth() == w && s.getHeight() == h) return s;
        throw new IllegalStateException("required size missing " + w + "x" + h);
    }

    private static Size largest(Size[] sizes) {
        if (sizes == null || sizes.length == 0) throw new IllegalStateException("no sizes");
        Size best = sizes[0];
        for (Size s : sizes)
            if ((long) s.getWidth() * s.getHeight() > (long) best.getWidth() * best.getHeight()) best = s;
        return best;
    }

    private static String ints(int[] v) { return v == null ? "null" : Arrays.toString(v); }
    private static String size(Size s) { return s == null ? "null" : s.toString(); }
    private static String rect(android.graphics.Rect r) { return r == null ? "null" : r.toShortString(); }

    private interface ThrowingRunnable { void run() throws Exception; }

    private static final class CamCtx {
        final CameraDevice camera;
        final HandlerThread thread;
        final Handler handler;
        CamCtx(CameraDevice camera, HandlerThread thread, Handler handler) {
            this.camera = camera;
            this.thread = thread;
            this.handler = handler;
        }
    }
}
