package com.android.support;

import android.graphics.PixelFormat;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.view.Surface;
import android.view.SurfaceControl;

import java.io.BufferedReader;
import java.io.InputStreamReader;

public class V2Overlay {
    static {
        try {
            System.load("/data/local/tmp/libv2mod.so");
        } catch (Throwable t) {
            System.loadLibrary("v2mod");
        }
    }

    private static native int nativeRun(Surface surface, int width, int height);

    public static void main(String[] args) {
        final int[] size = parseSize(args);
        Looper.prepareMainLooper();
        Handler h = new Handler(Looper.getMainLooper());
        h.post(() -> start(size[0], size[1]));
        Looper.loop();
    }

    private static int[] parseSize(String[] args) {
        int w = 0, h = 0;
        if (args != null) {
            for (int i = 0; i < args.length; i++) {
                if ("--width".equals(args[i]) && i + 1 < args.length) {
                    w = parseInt(args[++i], 0);
                } else if ("--height".equals(args[i]) && i + 1 < args.length) {
                    h = parseInt(args[++i], 0);
                }
            }
        }
        if (w <= 0 || h <= 0) {
            int[] wm = readWmSize();
            if (w <= 0) w = wm[0];
            if (h <= 0) h = wm[1];
        }
        if (w <= 0 || h <= 0) {
            int[] fallback = readDisplayFallback();
            if (w <= 0) w = fallback[0];
            if (h <= 0) h = fallback[1];
        }
        if (w <= 0) w = 1080;
        if (h <= 0) h = 2400;
        return new int[]{w, h};
    }

    private static int parseInt(String s, int def) {
        try { return Integer.parseInt(s.trim()); } catch (Throwable t) { return def; }
    }

    private static int[] readWmSize() {
        int w = 0, h = 0;
        try {
            Process p = new ProcessBuilder("sh", "-c", "wm size 2>/dev/null | tail -n1")
                    .redirectErrorStream(true).start();
            BufferedReader br = new BufferedReader(new InputStreamReader(p.getInputStream()));
            String line = br.readLine();
            br.close();
            p.waitFor();
            if (line != null) {
                String nums = line.replaceAll("[^0-9x]", "");
                int x = nums.indexOf('x');
                if (x > 0) {
                    w = parseInt(nums.substring(0, x), 0);
                    h = parseInt(nums.substring(x + 1), 0);
                }
            }
        } catch (Throwable ignored) {}
        return new int[]{w, h};
    }

    private static int[] readDisplayFallback() {
        try {
            Class<?> dmg = Class.forName("android.hardware.display.DisplayManagerGlobal");
            Object inst = dmg.getMethod("getInstance").invoke(null);
            Object info = dmg.getMethod("getDisplayInfo", int.class)
                    .invoke(inst, 0 /* Display.DEFAULT_DISPLAY */);
            Class<?> dic = Class.forName("android.view.DisplayInfo");
            int w = dic.getField("logicalWidth").getInt(info);
            int h = dic.getField("logicalHeight").getInt(info);
            return new int[]{Math.max(w, 0), Math.max(h, 0)};
        } catch (Throwable t) {
            System.err.println("[v2mod] display fallback failed: " + t);
            return new int[]{0, 0};
        }
    }

    private static void start(int w, int h) {
        if (w <= 0 || h <= 0) {
            System.err.println("[v2mod] invalid display size: " + w + "x" + h);
            System.exit(1);
            return;
        }
        System.err.println("[v2mod] display size " + w + "x" + h);
        SurfaceControl sc = null;
        Surface surface = null;
        try {
            SurfaceControl.Builder b = new SurfaceControl.Builder()
                    .setName("v2mod-overlay")
                    .setBufferSize(w, h)
                    .setFormat(PixelFormat.TRANSLUCENT);
            if (Build.VERSION.SDK_INT >= 33) {
                try { b.setOpaque(false); } catch (Throwable ignored) {}
            }
            sc = b.build();
            if (sc == null || !sc.isValid()) {
                System.err.println("[v2mod] SurfaceControl build failed");
                System.exit(1);
                return;
            }

            surface = new Surface(sc);
            if (!surface.isValid()) {
                System.err.println("[v2mod] Surface is invalid");
                release(sc, surface);
                System.exit(1);
                return;
            }

            SurfaceControl.Transaction tx = new SurfaceControl.Transaction();
            tx.setLayer(sc, Integer.MAX_VALUE - 1);
            tx.setAlpha(sc, 1.0f);
            tx.setVisibility(sc, true);
            tx.apply();
            System.err.println("[v2mod] SurfaceControl layer applied");

            final SurfaceControl fsc = sc;
            final Surface fs = surface;
            final int fw = w, fh = h;
            Thread t = new Thread(() -> {
                int rc = 1;
                try {
                    rc = nativeRun(fs, fw, fh);
                } catch (Throwable e) {
                    e.printStackTrace();
                } finally {
                    cleanupSurface(fsc, fs);
                    System.err.println("[v2mod] native exited rc=" + rc);
                    System.exit(rc == 0 ? 0 : 1);
                }
            }, "v2mod-native");
            t.setDaemon(false);
            t.start();
        } catch (Throwable e) {
            e.printStackTrace();
            cleanupSurface(sc, surface);
            System.exit(1);
        }
    }

    private static void cleanupSurface(SurfaceControl sc, Surface surface) {
        if (surface != null) {
            try { surface.release(); } catch (Throwable ignored) {}
        }
        if (sc != null) {
            try {
                SurfaceControl.Transaction tx = new SurfaceControl.Transaction();
                tx.reparent(sc, null);
                tx.apply();
            } catch (Throwable ignored) {}
            try { sc.release(); } catch (Throwable ignored) {}
        }
    }

    private static void release(SurfaceControl sc, Surface surface) {
        cleanupSurface(sc, surface);
    }
}
