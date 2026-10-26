package org.pjarczak.t311btscan;

import android.Manifest;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.util.Log;

public final class BluetoothProbe extends Service {
    private static final String TAG = "T311BTSCAN";
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final BluetoothAdapter adapter = BluetoothAdapter.getDefaultAdapter();
    private int found;
    private boolean registered;
    private final BroadcastReceiver receiver = new BroadcastReceiver() {
        @Override public void onReceive(Context context, Intent intent) {
            String action = intent.getAction();
            if (BluetoothDevice.ACTION_FOUND.equals(action)) {
                found++;
                Log.i(TAG, "BT_FOUND events=" + found);
            } else if (BluetoothAdapter.ACTION_DISCOVERY_FINISHED.equals(action)) {
                Log.i(TAG, "BT_DISCOVERY_FINISHED totalEvents=" + found);
                stopSelf();
            }
        }
    };
    @Override public void onCreate() {
        super.onCreate();
        ((NotificationManager) getSystemService(NOTIFICATION_SERVICE)).createNotificationChannel(
            new NotificationChannel("t311_bt", "Bluetooth hardware test", NotificationManager.IMPORTANCE_LOW));
        startForeground(312, new Notification.Builder(this, "t311_bt")
            .setSmallIcon(android.R.drawable.ic_menu_search)
            .setContentTitle("Bluetooth diagnostics")
            .setContentText("Counting nearby discoverable devices")
            .build());
    }
    @Override public int onStartCommand(Intent intent, int flags, int startId) {
        if (adapter == null || !adapter.isEnabled() ||
            checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) != PackageManager.PERMISSION_GRANTED) {
            Log.e(TAG, "BT_PREREQUISITE_FAILED");
            stopSelf();
            return START_NOT_STICKY;
        }
        IntentFilter filter = new IntentFilter(BluetoothDevice.ACTION_FOUND);
        filter.addAction(BluetoothAdapter.ACTION_DISCOVERY_FINISHED);
        registerReceiver(receiver, filter);
        registered = true;
        boolean ok = adapter.startDiscovery();
        Log.i(TAG, "BT_DISCOVERY_START result=" + ok);
        handler.postDelayed(() -> {
            Log.i(TAG, "BT_SCAN_FINAL totalEvents=" + found);
            stopSelf();
        }, 25000);
        return START_NOT_STICKY;
    }
    @Override public void onDestroy() {
        handler.removeCallbacksAndMessages(null);
        if (adapter != null && adapter.isDiscovering()) adapter.cancelDiscovery();
        if (registered) unregisterReceiver(receiver);
        super.onDestroy();
    }
    @Override public IBinder onBind(Intent intent) { return null; }
}
