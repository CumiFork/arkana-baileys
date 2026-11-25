#pragma once
#include <android/native_window.h>
#include <cstdint>

struct NativeOverlay {
    ANativeWindow* window = nullptr;
    void* surfaceControl = nullptr; // sp<SurfaceControl> storage
    void* client = nullptr;
    int width = 0;
    int height = 0;
};

bool NativeOverlay_Create(NativeOverlay* out, int width, int height);
void NativeOverlay_Destroy(NativeOverlay* out);
