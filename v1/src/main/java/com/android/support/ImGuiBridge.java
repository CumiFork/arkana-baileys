package com.android.support;

import android.view.Surface;

public final class ImGuiBridge {
    private ImGuiBridge() {}

    public static native void nativeOnSurfaceCreated(Surface surface);
    public static native void nativeOnSurfaceChanged(int width, int height);
    public static native void nativeOnDrawFrame();
    public static native void nativeOnTouch(float x, float y, int action);
    public static native boolean nativeIsMenuVisible();
    public static native boolean nativeWantCapture();
    public static native void nativeSetRootStatus(String status);
    public static native void nativeOnSurfaceDestroyed();
}
