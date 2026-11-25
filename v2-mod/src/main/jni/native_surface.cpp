#include "native_surface.h"
#include <android/log.h>
#include <dlfcn.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <string>

#define LOG_TAG "MEOW_V2SURF"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace {
template<typename T> inline T fn(void* lib, const char* name) {
    void* s = dlsym(lib, name);
    if (!s) LOGW("dlsym missing: %s", name);
    return reinterpret_cast<T>(s);
}

struct String8 {
    char data[128];
    String8() { memset(data, 0, sizeof(data)); }
};

// sp<T> is typically a single pointer on Android
struct sp { void* ptr = nullptr; };

bool g_libsLoaded = false;
void* g_libgui = nullptr;
void* g_libutils = nullptr;
void* g_libui = nullptr;

using String8Ctor = void (*)(void*, const char*);
using String8Dtor = void (*)(void*);
using SCCCtor = void (*)(void*);
using SCCDtor = void (*)(void*);
// arm64: sp<SurfaceControl> returned via hidden sret pointer (x8)
using SCCCreateSurface = void (*)(void* outSpSurfaceControl, void* client, const void* name, uint32_t w, uint32_t h, int32_t format, uint32_t flags, const void* parentHandle, const void* metadata, uint32_t* outTransformHint);
using SCGetSurface = void (*)(void* outSpSurface, void* surfaceControl);
using TxCtor = void (*)(void*);
using TxDtor = void (*)(void*);
using TxSetLayer = void* (*)(void*, const void* sc, int32_t z);
using TxSetPosition = void* (*)(void*, const void* sc, float x, float y);
using TxSetAlpha = void* (*)(void*, const void* sc, float alpha);
using TxShow = void* (*)(void*, const void* sc);
using TxApply = int32_t (*)(void*, bool synchronous, bool oneWay);

String8Ctor s8_ctor = nullptr;
String8Dtor s8_dtor = nullptr;
SCCCtor scc_ctor = nullptr;
SCCCreateSurface scc_createSurface = nullptr;
SCGetSurface sc_getSurface = nullptr;
TxCtor tx_ctor = nullptr;
TxDtor tx_dtor = nullptr;
TxSetLayer tx_setLayer = nullptr;
TxSetPosition tx_setPosition = nullptr;
TxSetAlpha tx_setAlpha = nullptr;
TxShow tx_show = nullptr;
TxApply tx_apply = nullptr;

bool loadLibs() {
    if (g_libsLoaded) return true;
    g_libgui = dlopen("libgui.so", RTLD_NOW | RTLD_GLOBAL);
    g_libutils = dlopen("libutils.so", RTLD_NOW | RTLD_GLOBAL);
    g_libui = dlopen("libui.so", RTLD_NOW | RTLD_GLOBAL);
    if (!g_libgui) { LOGE("dlopen libgui.so failed: %s", dlerror()); return false; }
    if (!g_libutils) { LOGE("dlopen libutils.so failed: %s", dlerror()); return false; }

    s8_ctor = fn<String8Ctor>(g_libutils, "_ZN7android7String8C1EPKc");
    s8_dtor = fn<String8Dtor>(g_libutils, "_ZN7android7String8D1Ev");
    scc_ctor = fn<SCCCtor>(g_libgui, "_ZN7android21SurfaceComposerClientC1Ev");
    scc_createSurface = fn<SCCCreateSurface>(g_libgui, "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjiiRKNS_2spINS_7IBinderEEENS_3gui13LayerMetadataEPj");
    sc_getSurface = fn<SCGetSurface>(g_libgui, "_ZN7android14SurfaceControl10getSurfaceEv");
    tx_ctor = fn<TxCtor>(g_libgui, "_ZN7android21SurfaceComposerClient11TransactionC1Ev");
    tx_dtor = fn<TxDtor>(g_libgui, "_ZN7android21SurfaceComposerClient11TransactionD1Ev");
    tx_setLayer = fn<TxSetLayer>(g_libgui, "_ZN7android21SurfaceComposerClient11Transaction8setLayerERKNS_2spINS_14SurfaceControlEEEi");
    tx_setPosition = fn<TxSetPosition>(g_libgui, "_ZN7android21SurfaceComposerClient11Transaction11setPositionERKNS_2spINS_14SurfaceControlEEEff");
    tx_setAlpha = fn<TxSetAlpha>(g_libgui, "_ZN7android21SurfaceComposerClient11Transaction8setAlphaERKNS_2spINS_14SurfaceControlEEEf");
    tx_show = fn<TxShow>(g_libgui, "_ZN7android21SurfaceComposerClient11Transaction4showERKNS_2spINS_14SurfaceControlEEE");
    tx_apply = fn<TxApply>(g_libgui, "_ZN7android21SurfaceComposerClient11Transaction5applyEbb");

    g_libsLoaded = s8_ctor && s8_dtor && scc_ctor && scc_createSurface && sc_getSurface && tx_ctor && tx_apply;
    LOGI("native surface libs loaded=%d", g_libsLoaded);
    return g_libsLoaded;
}

}  // namespace

bool NativeOverlay_Create(NativeOverlay* out, int width, int height) {
    if (!out) return false;
    if (!loadLibs()) return false;

    // SurfaceComposerClient
    void* clientBuf = malloc(256);
    memset(clientBuf, 0, 256);
    scc_ctor(clientBuf);
    out->client = clientBuf;

    // String8 name
    String8 name{};
    s8_ctor(name.data, "v2mod-overlay");

    // parent binder sp (null)
    sp parent{};
    // gui::LayerMetadata - allocate zeroed buffer; size varies, 256 bytes should be enough
    void* metadata = malloc(256);
    memset(metadata, 0, 256);

    // output sp<SurfaceControl>
    sp surfaceControl{};
    uint32_t outTransformHint = 0;

    // PixelFormat RGBA_8888 = 1, flags = 0x40 (eHidden?) or 0; try 0
    int32_t format = 1; // PIXEL_FORMAT_RGBA_8888
    uint32_t flags = 0;
    scc_createSurface(&surfaceControl, clientBuf, name.data, (uint32_t)width, (uint32_t)height, format, flags, &parent, metadata, &outTransformHint);
    s8_dtor(name.data);
    free(metadata);

    if (!surfaceControl.ptr) {
        LOGE("createSurface returned null SurfaceControl");
        free(clientBuf); out->client = nullptr;
        return false;
    }
    LOGI("SurfaceControl created: %p", surfaceControl.ptr);

    // store sp<SurfaceControl> (we keep it alive by keeping the sp storage)
    void* scStorage = malloc(sizeof(sp));
    memcpy(scStorage, &surfaceControl, sizeof(sp));
    out->surfaceControl = scStorage;

    // Configure transaction
    void* txBuf = malloc(1024);
    memset(txBuf, 0, 1024);
    tx_ctor(txBuf);
    if (tx_setLayer) tx_setLayer(txBuf, scStorage, 0x7FFFFFFF); // top layer
    if (tx_setPosition) tx_setPosition(txBuf, scStorage, 0.0f, 0.0f);
    if (tx_setAlpha) tx_setAlpha(txBuf, scStorage, 1.0f);
    if (tx_show) tx_show(txBuf, scStorage);
    int32_t applyStatus = tx_apply ? tx_apply(txBuf, false, false) : -1;
    if (tx_dtor) tx_dtor(txBuf);
    free(txBuf);
    LOGI("Transaction apply status=%d", applyStatus);

    // Get ANativeWindow from SurfaceControl
    sp surface{};
    sc_getSurface(&surface, surfaceControl.ptr);
    if (!surface.ptr) {
        LOGE("SurfaceControl::getSurface returned null");
        return false;
    }
    // android::Surface inherits ANativeWindow; raw pointer is usable as ANativeWindow*
    out->window = reinterpret_cast<ANativeWindow*>(surface.ptr);
    out->width = width;
    out->height = height;
    LOGI("NativeOverlay window=%p %dx%d", out->window, width, height);
    return true;
}

void NativeOverlay_Destroy(NativeOverlay* out) {
    if (!out) return;
    // best-effort cleanup; process exit will reclaim
    if (out->surfaceControl) { free(out->surfaceControl); out->surfaceControl = nullptr; }
    if (out->client) { free(out->client); out->client = nullptr; }
    out->window = nullptr; // owned by Surface
}
