package com.android.support;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.util.Log;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

public class MainActivity extends Activity {
    private static final String TAG = "MEOW_V2EXT";
    private TextView statusText;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        ScrollView scroll = new ScrollView(this);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        int pad = dp(16);
        root.setPadding(pad, pad, pad, pad);

        TextView title = new TextView(this);
        title.setText("v2-mod External ESP Overlay");
        title.setTextSize(20f);
        root.addView(title);

        statusText = new TextView(this);
        statusText.setText("Loading native library...");
        statusText.setPadding(0, dp(12), 0, dp(12));
        root.addView(statusText);

        Button overlayBtn = new Button(this);
        overlayBtn.setText("Grant Overlay Permission");
        overlayBtn.setOnClickListener(v -> requestOverlay());
        root.addView(overlayBtn);

        Button startBtn = new Button(this);
        startBtn.setText("Start External Overlay");
        startBtn.setOnClickListener(v -> startOverlay());
        root.addView(startBtn);

        Button stopBtn = new Button(this);
        stopBtn.setText("Stop Overlay");
        stopBtn.setOnClickListener(v -> stopOverlay());
        root.addView(stopBtn);

        TextView note = new TextView(this);
        note.setText("\nExternal mode: reads /proc/<pid>/mem as root. No injection into game process.\nRequires root and overlay permission.\nTarget: com.mobile.legends:UnityKillsMe");
        note.setPadding(0, dp(12), 0, 0);
        root.addView(note);

        scroll.addView(root);
        setContentView(scroll);
        loadNative();
    }

    private void loadNative() {
        try {
            System.loadLibrary("MEOW");
            String info = NativeBridge.nativeGetSystemInfo();
            statusText.setText("Native loaded.\n\n" + info);
        } catch (Throwable t) {
            String detail = "Native error: " + t.getMessage();
            Log.e(TAG, detail, t);
            statusText.setText(detail);
            Toast.makeText(this, detail, Toast.LENGTH_LONG).show();
        }
    }

    private void requestOverlay() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M && !Settings.canDrawOverlays(this)) {
            Intent intent = new Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION, Uri.parse("package:" + getPackageName()));
            startActivity(intent);
        } else {
            Toast.makeText(this, "Overlay permission already granted", Toast.LENGTH_SHORT).show();
        }
    }

    private void startOverlay() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M && !Settings.canDrawOverlays(this)) {
            Toast.makeText(this, "Grant overlay permission first", Toast.LENGTH_SHORT).show();
            requestOverlay();
            return;
        }
        Intent intent = new Intent(this, Menu.class);
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) startForegroundService(intent);
            else startService(intent);
        } catch (Throwable t) {
            Log.e(TAG, "Start overlay failed", t);
            Toast.makeText(this, "Start failed: " + t.getMessage(), Toast.LENGTH_LONG).show();
        }
    }

    private void stopOverlay() { stopService(new Intent(this, Menu.class)); }

    private int dp(int v) { return Math.round(getResources().getDisplayMetrics().density * v); }
}
