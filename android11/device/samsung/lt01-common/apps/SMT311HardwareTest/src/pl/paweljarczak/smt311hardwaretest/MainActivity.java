package pl.paweljarczak.smt311hardwaretest;

import android.Manifest;
import android.app.Activity;
import android.content.Context;
import android.content.pm.PackageManager;
import android.location.GnssStatus;
import android.location.Location;
import android.location.LocationListener;
import android.location.LocationManager;
import android.media.AudioFormat;
import android.media.AudioRecord;
import android.media.AudioTrack;
import android.media.MediaRecorder;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.util.Locale;

public final class MainActivity extends Activity {
    private static final int REQUEST_PERMISSIONS = 311;
    private static final int SAMPLE_RATE = 44100;
    private final Handler mainHandler = new Handler(Looper.getMainLooper());

    private TextView status;
    private LocationManager locationManager;
    private AudioTrack recordedTrack;
    private short[] recordedAudio;
    private volatile boolean recording;
    private boolean gpsActive;

    private final LocationListener locationListener = new LocationListener() {
        @Override
        public void onLocationChanged(Location location) {
            append(String.format(Locale.US,
                    "GPS FIX: %.6f, %.6f  accuracy=%.1fm  speed=%.1fm/s",
                    location.getLatitude(), location.getLongitude(),
                    location.getAccuracy(), location.getSpeed()));
        }

        @Override public void onProviderEnabled(String provider) { append("GPS provider enabled"); }
        @Override public void onProviderDisabled(String provider) { append("GPS provider disabled"); }
        @Override public void onStatusChanged(String provider, int state, Bundle extras) { }
    };

    private final GnssStatus.Callback gnssCallback = new GnssStatus.Callback() {
        @Override public void onStarted() { append("GNSS engine started"); }
        @Override public void onStopped() { append("GNSS engine stopped"); }
        @Override public void onFirstFix(int ttffMillis) { append("GNSS first fix: " + ttffMillis + " ms"); }

        @Override
        public void onSatelliteStatusChanged(GnssStatus value) {
            int used = 0;
            for (int index = 0; index < value.getSatelliteCount(); index++) {
                if (value.usedInFix(index)) used++;
            }
            append("Satellites: visible=" + value.getSatelliteCount() + " used=" + used);
        }
    };

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        locationManager = (LocationManager) getSystemService(Context.LOCATION_SERVICE);

        LinearLayout body = new LinearLayout(this);
        body.setOrientation(LinearLayout.VERTICAL);
        int pad = (int) (16 * getResources().getDisplayMetrics().density);
        body.setPadding(pad, pad, pad, pad);

        status = new TextView(this);
        status.setTextIsSelectable(true);
        status.setText("SM-T311 hardware test ready\n");

        addButton(body, "Hardware status", view -> showStatus());
        addButton(body, "Start GPS", view -> startGps());
        addButton(body, "Stop GPS", view -> stopGps());
        addButton(body, "Play 440 Hz tone", view -> playTone());
        addButton(body, "Record microphone 3 s", view -> recordMicrophone());
        addButton(body, "Play microphone recording", view -> playRecording());
        addButton(body, "Clear log", view -> status.setText(""));
        body.addView(status);

        ScrollView scroll = new ScrollView(this);
        scroll.addView(body);
        setContentView(scroll);
        requestPermissionsIfNeeded();
        showStatus();
    }

    private void addButton(LinearLayout parent, String text, View.OnClickListener listener) {
        Button button = new Button(this);
        button.setText(text);
        button.setOnClickListener(listener);
        parent.addView(button);
    }

    private void append(String value) {
        mainHandler.post(() -> status.append(value + "\n"));
    }

    private boolean hasPermission(String permission) {
        return checkSelfPermission(permission) == PackageManager.PERMISSION_GRANTED;
    }

    private void requestPermissionsIfNeeded() {
        if (!hasPermission(Manifest.permission.ACCESS_FINE_LOCATION)
                || !hasPermission(Manifest.permission.RECORD_AUDIO)) {
            requestPermissions(new String[] {
                    Manifest.permission.ACCESS_FINE_LOCATION,
                    Manifest.permission.ACCESS_COARSE_LOCATION,
                    Manifest.permission.RECORD_AUDIO
            }, REQUEST_PERMISSIONS);
        }
    }

    private void showStatus() {
        boolean gpsProvider = locationManager != null
                && locationManager.getAllProviders().contains(LocationManager.GPS_PROVIDER);
        int playMin = AudioTrack.getMinBufferSize(SAMPLE_RATE,
                AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT);
        int recordMin = AudioRecord.getMinBufferSize(SAMPLE_RATE,
                AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT);
        append("GPS provider=" + gpsProvider);
        append("GPS enabled=" + (gpsProvider && locationManager.isProviderEnabled(LocationManager.GPS_PROVIDER)));
        append("AudioTrack min buffer=" + playMin);
        append("AudioRecord min buffer=" + recordMin);
    }

    private void startGps() {
        if (gpsActive) {
            append("GPS test already active");
            return;
        }
        if (!hasPermission(Manifest.permission.ACCESS_FINE_LOCATION)) {
            append("ERROR: missing location permission");
            requestPermissionsIfNeeded();
            return;
        }
        if (locationManager == null
                || !locationManager.getAllProviders().contains(LocationManager.GPS_PROVIDER)) {
            append("ERROR: GPS provider is absent");
            return;
        }
        try {
            if (!locationManager.registerGnssStatusCallback(gnssCallback, mainHandler)) {
                append("ERROR: GNSS callback registration failed");
                return;
            }
            locationManager.requestLocationUpdates(
                    LocationManager.GPS_PROVIDER, 1000L, 0.0f,
                    locationListener, Looper.getMainLooper());
            gpsActive = true;
            append("GPS test started - move outdoors");
        } catch (RuntimeException exception) {
            append("GPS start failed: " + exception);
        }
    }

    private void stopGps() {
        if (locationManager == null || !gpsActive) return;
        locationManager.removeUpdates(locationListener);
        locationManager.unregisterGnssStatusCallback(gnssCallback);
        gpsActive = false;
        append("GPS test stopped");
    }

    private void playTone() {
        new Thread(() -> {
            int frames = SAMPLE_RATE * 2;
            short[] data = new short[frames];
            for (int index = 0; index < frames; index++) {
                data[index] = (short) (Math.sin(2.0 * Math.PI * 440.0 * index / SAMPLE_RATE) * 10000.0);
            }
            int min = AudioTrack.getMinBufferSize(SAMPLE_RATE,
                    AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT);
            if (min <= 0) {
                append("AudioTrack min buffer failed: " + min);
                return;
            }
            AudioTrack track;
            try {
                track = new AudioTrack.Builder()
                        .setAudioFormat(new AudioFormat.Builder()
                                .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                                .setSampleRate(SAMPLE_RATE)
                                .setChannelMask(AudioFormat.CHANNEL_OUT_MONO)
                                .build())
                        .setBufferSizeInBytes(Math.max(min, SAMPLE_RATE * 2))
                        .setTransferMode(AudioTrack.MODE_STREAM)
                        .build();
            } catch (RuntimeException exception) {
                append("AudioTrack create failed: " + exception);
                return;
            }
            if (track.getState() != AudioTrack.STATE_INITIALIZED) {
                append("AudioTrack is not initialized");
                track.release();
                return;
            }
            append("Playing 440 Hz tone for 2 seconds");
            track.play();
            int written = track.write(data, 0, data.length, AudioTrack.WRITE_BLOCKING);
            if (written != data.length) append("AudioTrack wrote " + written + "/" + data.length + " samples");
            track.stop();
            track.release();
            append("Tone playback finished");
        }, "SMT311Tone").start();
    }

    private void recordMicrophone() {
        if (recording) {
            append("Microphone recording already active");
            return;
        }
        if (!hasPermission(Manifest.permission.RECORD_AUDIO)) {
            append("ERROR: missing microphone permission");
            requestPermissionsIfNeeded();
            return;
        }
        recording = true;
        new Thread(() -> {
            int min = AudioRecord.getMinBufferSize(SAMPLE_RATE,
                    AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT);
            if (min <= 0) {
                append("AudioRecord min buffer failed: " + min);
                recording = false;
                return;
            }
            AudioRecord recorder;
            try {
                recorder = new AudioRecord.Builder()
                        .setAudioSource(MediaRecorder.AudioSource.MIC)
                        .setAudioFormat(new AudioFormat.Builder()
                                .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                                .setSampleRate(SAMPLE_RATE)
                                .setChannelMask(AudioFormat.CHANNEL_IN_MONO)
                                .build())
                        .setBufferSizeInBytes(Math.max(min, SAMPLE_RATE * 2))
                        .build();
            } catch (RuntimeException exception) {
                append("AudioRecord create failed: " + exception);
                recording = false;
                return;
            }
            if (recorder.getState() != AudioRecord.STATE_INITIALIZED) {
                append("AudioRecord is not initialized");
                recorder.release();
                recording = false;
                return;
            }
            short[] output = new short[SAMPLE_RATE * 3];
            int offset = 0;
            try {
                recorder.startRecording();
                append("Recording microphone for 3 seconds");
                while (offset < output.length) {
                    int count = recorder.read(output, offset, output.length - offset,
                            AudioRecord.READ_BLOCKING);
                    if (count <= 0) {
                        append("AudioRecord read failed: " + count);
                        break;
                    }
                    offset += count;
                }
            } catch (RuntimeException exception) {
                append("AudioRecord failed: " + exception);
            } finally {
                try { recorder.stop(); } catch (RuntimeException ignored) { }
                recorder.release();
            }
            if (offset > 0) {
                recordedAudio = new short[offset];
                System.arraycopy(output, 0, recordedAudio, 0, offset);
                append("Microphone recording ready: " + offset + " samples");
            }
            recording = false;
        }, "SMT311AudioRecord").start();
    }

    private void playRecording() {
        short[] data = recordedAudio;
        if (data == null || data.length == 0) {
            append("No microphone recording available");
            return;
        }
        if (recordedTrack != null) {
            recordedTrack.release();
            recordedTrack = null;
        }
        recordedTrack = new AudioTrack.Builder()
                .setAudioFormat(new AudioFormat.Builder()
                        .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                        .setSampleRate(SAMPLE_RATE)
                        .setChannelMask(AudioFormat.CHANNEL_OUT_MONO)
                        .build())
                .setBufferSizeInBytes(data.length * 2)
                .setTransferMode(AudioTrack.MODE_STATIC)
                .build();
        int written = recordedTrack.write(data, 0, data.length);
        if (written <= 0) {
            append("Recorded audio write failed: " + written);
            recordedTrack.release();
            recordedTrack = null;
            return;
        }
        recordedTrack.play();
        append("Playing microphone recording");
    }

    @Override
    protected void onDestroy() {
        stopGps();
        if (recordedTrack != null) recordedTrack.release();
        super.onDestroy();
    }
}
