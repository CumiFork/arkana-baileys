package com.android.support;

public final class NativeBridge {
    private NativeBridge() {}
    public static native String nativeGetSystemInfo();
    public static native String nativeGetCpuLoad();
}
