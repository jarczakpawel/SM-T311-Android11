package org.pjarczak.t311sensorsprobe;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Intent;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.util.Log;

public final class SensorProbeService extends Service implements SensorEventListener {
    private static final String TAG = "T311SENSORS";
    private static final int[] TYPES = {
        Sensor.TYPE_ACCELEROMETER, Sensor.TYPE_MAGNETIC_FIELD,
        Sensor.TYPE_LIGHT, Sensor.TYPE_PROXIMITY, Sensor.TYPE_GRAVITY,
        Sensor.TYPE_ROTATION_VECTOR, Sensor.TYPE_GYROSCOPE,
        Sensor.TYPE_PRESSURE
    };
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final int[] counts = new int[TYPES.length];
    private SensorManager manager;
    private boolean started;

    @Override public void onCreate() {
        super.onCreate();
        ((NotificationManager) getSystemService(NOTIFICATION_SERVICE))
            .createNotificationChannel(new NotificationChannel("t311_sensors", "Sensor diagnostics", NotificationManager.IMPORTANCE_LOW));
        startForeground(313, new Notification.Builder(this, "t311_sensors")
            .setSmallIcon(android.R.drawable.ic_menu_compass)
            .setContentTitle("SM-T311 sensors")
            .setContentText("Checking sensor callbacks")
            .build());
    }

    @Override public int onStartCommand(Intent intent, int flags, int startId) {
        if (started) return START_NOT_STICKY;
        started = true;
        manager = (SensorManager) getSystemService(SENSOR_SERVICE);
        for (int i = 0; i < TYPES.length; ++i) {
            Sensor sensor = manager.getDefaultSensor(TYPES[i]);
            boolean active = sensor != null && manager.registerListener(this, sensor, SensorManager.SENSOR_DELAY_NORMAL);
            Log.i(TAG, "SENSOR_START type=" + TYPES[i] + " present=" + (sensor != null) + " registered=" + active);
        }
        handler.postDelayed(() -> {
            for (int i = 0; i < TYPES.length; ++i)
                Log.i(TAG, "SENSOR_FINAL type=" + TYPES[i] + " callbacks=" + counts[i]);
            stopSelf();
        }, 20000);
        return START_NOT_STICKY;
    }

    @Override public void onSensorChanged(SensorEvent event) {
        for (int i = 0; i < TYPES.length; ++i) {
            if (event.sensor.getType() == TYPES[i]) {
                counts[i]++;
                break;
            }
        }
    }

    @Override public void onAccuracyChanged(Sensor sensor, int accuracy) {}

    @Override public void onDestroy() {
        if (manager != null) manager.unregisterListener(this);
        handler.removeCallbacksAndMessages(null);
        super.onDestroy();
    }

    @Override public IBinder onBind(Intent intent) { return null; }
}
