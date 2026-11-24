package com.android.support;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.graphics.PixelFormat;
import android.opengl.GLSurfaceView;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.util.Log;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.WindowManager;
import android.widget.Toast;

import java.io.BufferedReader;
import java.io.InputStreamReader;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

public class Menu extends Service {
    private static final String TAG = "MEOW_V1";
    private static final String CHANNEL_ID = "v1_overlay";
    private static final int NOTIF_ID = 1001;

    private WindowManager windowManager;
    private GLSurfaceView glSurfaceView;
    private WindowManager.LayoutParams overlayParams;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Runnable restoreTouchableRunnable = new Runnable() {
        @Override public void run() { setOverlayTouchable(true); }
    };

    @Override
    public IBinder onBind(Intent intent) { return null; }

    @Override
    public void onCreate() {
        super.onCreate();
        startAsForeground();
        try {
            System.loadLibrary("MEOW");
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "Load MEOW failed", e);
            Toast.makeText(this, "Load MEOW failed: " + e.getMessage(), Toast.LENGTH_LONG).show();
            stopSelf();
            return;
        }
        createOverlay();
        refreshRootStatus();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        handler.removeCallbacks(restoreTouchableRunnable);
        try {
            if (glSurfaceView != null && windowManager != null) {
                windowManager.removeView(glSurfaceView);
            }
        } catch (Throwable ignored) {}
        glSurfaceView = null;
        super.onDestroy();
    }

    private void startAsForeground() {
        NotificationManager nm = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationChannel ch = new NotificationChannel(CHANNEL_ID, "Overlay", NotificationManager.IMPORTANCE_LOW);
            nm.createNotificationChannel(ch);
        }
        Notification.Builder b;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) b = new Notification.Builder(this, CHANNEL_ID);
        else b = new Notification.Builder(this);
        Notification notif = b.setContentTitle("ImGui Overlay")
                .setContentText("Read-only telemetry overlay active")
                .setSmallIcon(android.R.drawable.ic_menu_view)
                .setOngoing(true)
                .build();
        if (Build.VERSION.SDK_INT >= 34) {
            startForeground(NOTIF_ID, notif, ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE);
        } else {
            startForeground(NOTIF_ID, notif);
        }
    }

    private void createOverlay() {
        windowManager = (WindowManager) getSystemService(WINDOW_SERVICE);
        final GLSurfaceView glView = new GLSurfaceView(this);
        glView.setEGLContextClientVersion(3);
        glView.setEGLConfigChooser(8, 8, 8, 8, 16, 0);
        glView.getHolder().setFormat(PixelFormat.TRANSLUCENT);
        glView.setZOrderOnTop(true);
        glView.setRenderer(new GLSurfaceView.Renderer() {
            @Override public void onSurfaceCreated(GL10 gl, EGLConfig config) {
                try { ImGuiBridge.nativeOnSurfaceCreated(glView.getHolder().getSurface()); }
                catch (Throwable t) { Log.e(TAG, "onSurfaceCreated", t); }
            }
            @Override public void onSurfaceChanged(GL10 gl, int width, int height) {
                try { ImGuiBridge.nativeOnSurfaceChanged(width, height); }
                catch (Throwable t) { Log.e(TAG, "onSurfaceChanged", t); }
            }
            @Override public void onDrawFrame(GL10 gl) {
                try { ImGuiBridge.nativeOnDrawFrame(); }
                catch (Throwable t) { Log.e(TAG, "onDrawFrame", t); }
            }
        });
        glView.setRenderMode(GLSurfaceView.RENDERMODE_CONTINUOUSLY);
        glView.getHolder().addCallback(new SurfaceHolder.Callback() {
            @Override public void surfaceCreated(SurfaceHolder holder) {}
            @Override public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {}
            @Override public void surfaceDestroyed(SurfaceHolder holder) {
                try { ImGuiBridge.nativeOnSurfaceDestroyed(); }
                catch (Throwable t) { Log.e(TAG, "surfaceDestroyed", t); }
            }
        });

        int type = Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
                ? WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
                : WindowManager.LayoutParams.TYPE_PHONE;
        WindowManager.LayoutParams params = new WindowManager.LayoutParams(
                WindowManager.LayoutParams.MATCH_PARENT,
                WindowManager.LayoutParams.MATCH_PARENT,
                type,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN,
                PixelFormat.TRANSLUCENT
        );
        params.gravity = Gravity.TOP | Gravity.START;
        params.x = 0;
        params.y = 0;

        glView.setOnTouchListener((v, event) -> handleOverlayTouch(event));
        glSurfaceView = glView;
        overlayParams = params;
        windowManager.addView(glView, params);
    }

    private boolean handleOverlayTouch(MotionEvent event) {
        float x = event.getRawX();
        float y = event.getRawY();
        int action = event.getActionMasked();
        boolean capture = false;
        try { capture = ImGuiBridge.nativeWantCapture(); } catch (Throwable ignored) {}

        if (!capture && action == MotionEvent.ACTION_DOWN) {
            setOverlayTouchable(false);
            handler.removeCallbacks(restoreTouchableRunnable);
            handler.postDelayed(restoreTouchableRunnable, 250);
            return false;
        }

        if (capture) {
            setOverlayTouchable(true);
            handler.removeCallbacks(restoreTouchableRunnable);
            try { ImGuiBridge.nativeOnTouch(x, y, action); } catch (Throwable t) { Log.e(TAG, "nativeOnTouch", t); }
            if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) {
                handler.postDelayed(restoreTouchableRunnable, 150);
            }
            return true;
        }

        if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) {
            handler.removeCallbacks(restoreTouchableRunnable);
            handler.postDelayed(restoreTouchableRunnable, 50);
        }
        return false;
    }

    private void setOverlayTouchable(boolean touchable) {
        if (glSurfaceView == null || overlayParams == null || windowManager == null) return;
        int baseFlags = WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN;
        int newFlags = touchable ? baseFlags : (baseFlags | WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE);
        if (overlayParams.flags != newFlags) {
            overlayParams.flags = newFlags;
            try { windowManager.updateViewLayout(glSurfaceView, overlayParams); } catch (Throwable ignored) {}
        }
    }

    private void refreshRootStatus() {
        new Thread(() -> {
            String status = "unavailable";
            try {
                Process p = Runtime.getRuntime().exec(new String[]{"su", "-c", "id"});
                BufferedReader br = new BufferedReader(new InputStreamReader(p.getInputStream()));
                String line = br.readLine();
                int code = p.waitFor();
                if (code == 0 && line != null && line.contains("uid=0")) status = "granted (" + line.trim() + ")";
                else status = "denied";
            } catch (Throwable t) {
                status = "unavailable";
            }
            final String s = status;
            handler.post(() -> {
                try { ImGuiBridge.nativeSetRootStatus(s); } catch (Throwable ignored) {}
            });
        }).start();
    }
}
