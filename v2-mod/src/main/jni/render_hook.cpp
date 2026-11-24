#include <android/log.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <pthread.h>
#include <unistd.h>
#include <dlfcn.h>
#include <cstdint>

#include <mutex>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "include/Hook/Dobby/dobby.h"
#include "menu.h"
#include "feature_state.h"
#include "xdl.h"

#define LOG_TAG "MEOW_V2"

namespace {

using EglSwapBuffersFn = EGLBoolean (*)(EGLDisplay, EGLSurface);
EglSwapBuffersFn orig_eglSwapBuffers = nullptr;
std::mutex g_renderMutex;
bool g_setup = false;
int g_width = 0;
int g_height = 0;

struct Vec2 { float x, y; };
struct UnityTouch {
    int m_FingerId;
    Vec2 m_Position;
    Vec2 m_RawPosition;
    Vec2 m_PositionDelta;
    float m_TimeDelta;
    int m_TapCount;
    int m_Phase;
    int m_Type;
    float m_Pressure;
    float m_maximumPossiblePressure;
    float m_Radius;
    float m_RadiusVariance;
    float m_AltitudeAngle;
    float m_AzimuthAngle;
};

typedef void* (*Il2CppClassFromNameFn)(const void* image, const char* namespaze, const char* name);
typedef void* (*Il2CppClassGetMethodFromNameFn)(void* klass, const char* name, int argsCount);
typedef void* (*Il2CppRuntimeInvokeFn)(void* method, void* obj, void** params, void** exc);
typedef void* (*Il2CppDomainGetFn)();
typedef void* (*Il2CppDomainGetAssembliesFn)(const void* domain, size_t* size);
typedef const void* (*Il2CppAssemblyGetImageFn)(const void* assembly);

Il2CppClassFromNameFn il2cpp_class_from_name = nullptr;
Il2CppClassGetMethodFromNameFn il2cpp_class_get_method_from_name = nullptr;
Il2CppRuntimeInvokeFn il2cpp_runtime_invoke = nullptr;
Il2CppDomainGetFn il2cpp_domain_get = nullptr;
Il2CppDomainGetAssembliesFn il2cpp_domain_get_assemblies = nullptr;
Il2CppAssemblyGetImageFn il2cpp_assembly_get_image = nullptr;

void* g_getTouchCount = nullptr;
void* g_getTouchMethod = nullptr;
bool g_touchInjected = false;
bool g_inputResolved = false;
bool g_inputTried = false;

void resolveIl2cppApi() {
    if (g_inputTried) return;
    g_inputTried = true;
    void* h = xdl_open("libil2cpp.so", XDL_DEFAULT);
    if (!h) return;
    il2cpp_class_from_name = (Il2CppClassFromNameFn)xdl_sym(h, "il2cpp_class_from_name", nullptr);
    il2cpp_class_get_method_from_name = (Il2CppClassGetMethodFromNameFn)xdl_sym(h, "il2cpp_class_get_method_from_name", nullptr);
    il2cpp_runtime_invoke = (Il2CppRuntimeInvokeFn)xdl_sym(h, "il2cpp_runtime_invoke", nullptr);
    il2cpp_domain_get = (Il2CppDomainGetFn)xdl_sym(h, "il2cpp_domain_get", nullptr);
    il2cpp_domain_get_assemblies = (Il2CppDomainGetAssembliesFn)xdl_sym(h, "il2cpp_domain_get_assemblies", nullptr);
    il2cpp_assembly_get_image = (Il2CppAssemblyGetImageFn)xdl_sym(h, "il2cpp_assembly_get_image", nullptr);
    if (!il2cpp_class_from_name || !il2cpp_class_get_method_from_name || !il2cpp_runtime_invoke) return;

    void* domain = il2cpp_domain_get ? il2cpp_domain_get() : nullptr;
    size_t asmCount = 0;
    void** assemblies = (domain && il2cpp_domain_get_assemblies) ? (void**)il2cpp_domain_get_assemblies(domain, &asmCount) : nullptr;
    void* inputClass = nullptr;
    for (size_t i = 0; i < asmCount && !inputClass; ++i) {
        const void* image = il2cpp_assembly_get_image ? il2cpp_assembly_get_image(assemblies[i]) : nullptr;
        if (!image) continue;
        inputClass = il2cpp_class_from_name(image, "UnityEngine", "Input");
    }
    if (!inputClass) {
        // fallback: try common image names null
        inputClass = il2cpp_class_from_name(nullptr, "UnityEngine", "Input");
    }
    if (!inputClass) return;

    g_getTouchCount = il2cpp_class_get_method_from_name(inputClass, "get_touchCount", 0);
    g_getTouchMethod = il2cpp_class_get_method_from_name(inputClass, "GetTouch_Injected", 2);
    g_touchInjected = g_getTouchMethod != nullptr;
    if (!g_getTouchMethod) {
        g_getTouchMethod = il2cpp_class_get_method_from_name(inputClass, "GetTouch", 1);
        g_touchInjected = false;
    }
    g_inputResolved = (g_getTouchCount && g_getTouchMethod);
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Input resolve: count=%p touch=%p injected=%d ok=%d", g_getTouchCount, g_getTouchMethod, g_touchInjected, g_inputResolved);
}

int getTouchCount() {
    if (!g_inputResolved) return 0;
    void* exc = nullptr;
    void* ret = il2cpp_runtime_invoke(g_getTouchCount, nullptr, nullptr, &exc);
    if (exc || !ret) return 0;
    // boxed int: Il2CppObject header then value. On 64-bit, value usually at +0x10
    return *reinterpret_cast<int*>((uintptr_t)ret + 0x10);
}

bool getTouch(int index, UnityTouch& out) {
    if (!g_inputResolved) return false;
    void* exc = nullptr;
    if (g_touchInjected) {
        void* params[2] = { &index, &out };
        il2cpp_runtime_invoke(g_getTouchMethod, nullptr, params, &exc);
        return exc == nullptr;
    }
    void* params[1] = { &index };
    void* ret = il2cpp_runtime_invoke(g_getTouchMethod, nullptr, params, &exc);
    if (exc || !ret) return false;
    // Boxed UnityEngine.Touch: value starts at +0x10
    out = *reinterpret_cast<UnityTouch*>((uintptr_t)ret + 0x10);
    return true;
}

void updateInput() {
    ImGuiIO& io = ImGui::GetIO();
    resolveIl2cppApi();
    if (!g_inputResolved) {
        io.MouseDown[0] = false;
        return;
    }
    int count = getTouchCount();
    if (count <= 0) {
        io.MouseDown[0] = false;
        return;
    }
    UnityTouch t{};
    if (!getTouch(0, t)) {
        io.MouseDown[0] = false;
        return;
    }
    float x = t.m_Position.x;
    float y = io.DisplaySize.y - t.m_Position.y;
    io.MousePos = ImVec2(x, y);
    // TouchPhase: Began=0, Moved=1, Stationary=2, Ended=3, Canceled=4
    if (t.m_Phase == 0 || t.m_Phase == 1 || t.m_Phase == 2) io.MouseDown[0] = true;
    else io.MouseDown[0] = false;
}

void updateSize(EGLDisplay dpy, EGLSurface surface) {
    EGLint w = 0, h = 0;
    if (eglQuerySurface(dpy, surface, EGL_WIDTH, &w) && eglQuerySurface(dpy, surface, EGL_HEIGHT, &h)) {
        if (w > 0 && h > 0) {
            g_width = w;
            g_height = h;
        }
    }
}

EGLBoolean hook_eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
    std::lock_guard<std::mutex> lock(g_renderMutex);
    if (!g_setup) {
        v2menu::SetupImGui();
        g_setup = true;
    }

    updateSize(dpy, surface);
    ImGuiIO& io = ImGui::GetIO();
    if (g_width > 0 && g_height > 0) io.DisplaySize = ImVec2((float)g_width, (float)g_height);
    if (io.DisplaySize.x <= 0 || io.DisplaySize.y <= 0) {
        return orig_eglSwapBuffers ? orig_eglSwapBuffers(dpy, surface) : EGL_FALSE;
    }

    updateInput();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    v2menu::DrawMenu();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    ImGui::EndFrame();

    return orig_eglSwapBuffers ? orig_eglSwapBuffers(dpy, surface) : EGL_FALSE;
}

void* hook_thread(void*) {
    for (int i = 0; i < 120; ++i) {
        void* handle = xdl_open("libEGL.so", XDL_DEFAULT);
        void* sym = handle ? xdl_sym(handle, "eglSwapBuffers", nullptr) : nullptr;
        if (sym) {
            int rc = DobbyHook(sym, (void*)hook_eglSwapBuffers, (void**)&orig_eglSwapBuffers);
            __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "eglSwapBuffers hook rc=%d sym=%p", rc, sym);
            if (rc == 0) return nullptr;
        }
        sleep(1);
    }
    __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "eglSwapBuffers hook failed");
    return nullptr;
}

}  // namespace

namespace v2menu {

void StartRenderHook() {
    pthread_t t;
    if (pthread_create(&t, nullptr, hook_thread, nullptr) == 0) {
        pthread_detach(t);
    }
}

}  // namespace v2menu
