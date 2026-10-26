package org.pjarczak.t311gnssstatus;

import android.Manifest;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.location.GnssStatus;
import android.location.Location;
import android.location.LocationListener;
import android.location.LocationManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.util.Log;

public final class GnssProbeService extends Service {
    private static final String TAG = "T311GNSS";
    private final Handler handler = new Handler(Looper.getMainLooper());
    private LocationManager manager;
    private boolean started;
    private int svEvents;
    private int fixes;
    // Signal strength only, NEVER PRN/SVID, coordinates or raw NMEA.
    private int maxVisible;
    private int maxPositiveCn0;
    private float maxCn0DbHz;

    private final GnssStatus.Callback status = new GnssStatus.Callback() {
        @Override public void onStarted() {
            Log.i(TAG, "GNSS_STARTED");
        }
        @Override public void onStopped() {
            Log.i(TAG, "GNSS_STOPPED");
        }
        @Override public void onFirstFix(int ttff) {
            Log.i(TAG, "GNSS_FIRST_FIX ttffMs=" + ttff);
        }
        @Override public void onSatelliteStatusChanged(GnssStatus s) {
            int used = 0;
            int strong = 0;
            int positiveCn0 = 0;
            float strongestCn0 = 0.0f;
            for (int i = 0; i < s.getSatelliteCount(); i++) {
                if (s.usedInFix(i)) used++;
                float cn0 = s.getCn0DbHz(i);
                if (Float.isFinite(cn0) && cn0 > 0.0f) positiveCn0++;
                if (Float.isFinite(cn0) && cn0 >= 25.0f) strong++;
                if (Float.isFinite(cn0) && cn0 > strongestCn0)
                    strongestCn0 = cn0;
            }
            if (s.getSatelliteCount() > maxVisible)
                maxVisible = s.getSatelliteCount();
            if (positiveCn0 > maxPositiveCn0)
                maxPositiveCn0 = positiveCn0;
            if (strongestCn0 > maxCn0DbHz)
                maxCn0DbHz = strongestCn0;
            svEvents++;
            if (svEvents == 1 || svEvents % 5 == 0)
                Log.i(TAG, "GNSS_SV visible=" + s.getSatelliteCount()
                        + " used=" + used + " events=" + svEvents
                        + " nonzeroCn0=" + positiveCn0
                        + " strong25=" + strong
                        + " strongestCn0=" + strongestCn0);
        }
    };
    private final LocationListener listener = new LocationListener() {
        @Override public void onLocationChanged(Location location) {
            fixes++;
            Log.i(TAG, "GNSS_FIX count=" + fixes + " accuracyM=" + location.getAccuracy());
        }
        @Override public void onStatusChanged(String provider, int status, Bundle extras) {}
        @Override public void onProviderEnabled(String provider) {}
        @Override public void onProviderDisabled(String provider) {}
    };

    @Override public void onCreate() {
        super.onCreate();
        ((NotificationManager) getSystemService(NOTIFICATION_SERVICE))
            .createNotificationChannel(new NotificationChannel("t311_gnss", "GNSS hardware test", NotificationManager.IMPORTANCE_LOW));
        startForeground(311, new Notification.Builder(this, "t311_gnss")
            .setSmallIcon(android.R.drawable.ic_menu_mylocation)
            .setContentTitle("GNSS diagnostics")
            .setContentText("Checking satellites without storing coordinates")
            .build());
    }

    @Override public int onStartCommand(Intent intent, int flags, int startId) {
        if (started) return START_NOT_STICKY;
        started = true;
        if (checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) != PackageManager.PERMISSION_GRANTED) {
            Log.e(TAG, "GNSS_PERMISSION_DENIED");
            stopSelf();
            return START_NOT_STICKY;
        }
        manager = (LocationManager) getSystemService(LOCATION_SERVICE);
        try {
            boolean registered = manager.registerGnssStatusCallback(status, handler);
            manager.requestLocationUpdates(LocationManager.GPS_PROVIDER, 1000, 0, listener, handler.getLooper());
            Log.i(TAG, "GNSS_REQUEST_STARTED callback=" + registered);
        } catch (Exception e) {
            Log.e(TAG, "GNSS_REQUEST_FAILED " + e.getClass().getSimpleName() + " " + e.getMessage());
            stopSelf();
            return START_NOT_STICKY;
        }
        handler.postDelayed(() -> {
            Log.i(TAG, "GNSS_FINAL svEvents=" + svEvents + " fixes=" + fixes
                    + " maxVisible=" + maxVisible
                    + " maxPositiveCn0=" + maxPositiveCn0
                    + " maxCn0=" + maxCn0DbHz);
            stopSelf();
        }, 70000);
        return START_NOT_STICKY;
    }

    @Override public void onDestroy() {
        if (manager != null) {
            manager.unregisterGnssStatusCallback(status);
            manager.removeUpdates(listener);
        }
        handler.removeCallbacksAndMessages(null);
        super.onDestroy();
    }

    @Override public IBinder onBind(Intent intent) {
        return null;
    }
}
