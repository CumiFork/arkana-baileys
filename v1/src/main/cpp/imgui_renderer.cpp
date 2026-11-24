#include <jni.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <cinttypes>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include "imgui.h"
#include "backends/imgui_impl_android.h"
#include "backends/imgui_impl_opengl3.h"
#include "core_engine.h"

#define LOG_TAG "MEOW_V1"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

ANativeWindow* g_Window = nullptr;
int g_Width = 0;
int g_Height = 0;
bool g_Initialized = false;
bool g_ShowMenu = true;
bool g_WantTextInput = false;

std::mutex g_FrameMutex;
std::vector<float> g_CpuHistory(120, 0.0f);
std::vector<float> g_MemHistory(120, 0.0f);
int g_HistoryIdx = 0;
std::string g_SystemInfo = "Loading...";
std::string g_Telemetry = "Telemetry off";
std::string g_Thermal = "Press refresh to read thermal zones.";
std::string g_Selinux = "Unknown";
std::string g_RootStatus = "Unknown";
std::string g_Log = "";
float g_LastTelemetry = 0.0f;

float readMemPercent() {
    std::ifstream in("/proc/meminfo");
    std::string key, unit;
    uint64_t value = 0, memTotal = 0, memAvailable = 0;
    while (in >> key >> value >> unit) {
        if (key == "MemTotal:") memTotal = value;
        if (key == "MemAvailable:") memAvailable = value;
    }
    if (memTotal == 0) return 0.0f;
    return (float)(memTotal - memAvailable) * 100.0f / (float)memTotal;
}

float parseCpuPercentFromTelemetry(const std::string& t) {
    // core::getCpuLoad returns "CPU: X.X%\n..."
    float v = 0.0f;
    if (sscanf(t.c_str(), "CPU: %f%%", &v) == 1) return v;
    return 0.0f;
}

void appendLog(const std::string& line) {
    if (line.empty()) return;
    if (!g_Log.empty()) g_Log += "\n";
    g_Log += line;
    if (g_Log.size() > 8000) {
        g_Log = g_Log.substr(g_Log.size() - 8000);
    }
}

void updateTelemetry(float dt) {
    g_LastTelemetry += dt;
    if (g_LastTelemetry < 0.5f) return;
    g_LastTelemetry = 0.0f;
    g_Telemetry = core::getCpuLoad();
    float cpu = parseCpuPercentFromTelemetry(g_Telemetry);
    float mem = readMemPercent();
    g_CpuHistory[g_HistoryIdx] = cpu;
    g_MemHistory[g_HistoryIdx] = mem;
    g_HistoryIdx = (g_HistoryIdx + 1) % (int)g_CpuHistory.size();
}

void drawMenu() {
    float w = g_Width > 0 ? (float)g_Width * 0.85f : 700.0f;
    float h = g_Height > 0 ? (float)g_Height * 0.45f : 500.0f;
    if (w < 320.0f) w = 320.0f;
    if (h < 280.0f) h = 280.0f;

    ImGui::SetNextWindowPos(ImVec2(40.0f, 150.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("ImGui Read-Only Overlay", &g_ShowMenu)) {
        if (ImGui::BeginTabBar("MainTabs")) {
            if (ImGui::BeginTabItem("System Telemetry")) {
                ImGui::TextWrapped("%s", g_SystemInfo.c_str());
                ImGui::Separator();
                ImGui::TextWrapped("%s", g_Telemetry.c_str());
                ImGui::PlotLines("CPU %", g_CpuHistory.data(), (int)g_CpuHistory.size(), g_HistoryIdx, nullptr, 0.0f, 100.0f, ImVec2(-1.0f, 80.0f));
                ImGui::PlotLines("RAM %", g_MemHistory.data(), (int)g_MemHistory.size(), g_HistoryIdx, nullptr, 0.0f, 100.0f, ImVec2(-1.0f, 80.0f));
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Hardware & Status")) {
                if (ImGui::Button("Refresh SELinux", ImVec2(-1.0f, 0.0f))) {
                    g_Selinux = core::getSelinuxStatus();
                    appendLog(g_Selinux);
                }
                if (ImGui::Button("Refresh Thermal", ImVec2(-1.0f, 0.0f))) {
                    g_Thermal = core::getThermalInfo();
                    appendLog("Thermal refreshed");
                }
                ImGui::TextWrapped("SELinux: %s", g_Selinux.c_str());
                ImGui::TextWrapped("Root: %s", g_RootStatus.c_str());
                ImGui::Separator();
                ImGui::BeginChild("thermal", ImVec2(0.0f, 160.0f), true);
                ImGui::TextWrapped("%s", g_Thermal.c_str());
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Read-Only Log Viewer")) {
                if (ImGui::Button("Clear Log", ImVec2(-1.0f, 0.0f))) g_Log.clear();
                ImGui::Separator();
                ImGui::BeginChild("logview", ImVec2(0.0f, 0.0f), true);
                ImGui::TextUnformatted(g_Log.empty() ? "(empty)" : g_Log.c_str());
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Auto Retribution")) {
                ImGui::TextWrapped("Status: Disabled");
                ImGui::Separator();
                ImGui::TextWrapped("This v1 build enforces 0%% code injection and strict read-only behavior.");
                ImGui::TextWrapped("Auto Retribution is not active because it would require external process memory access, hooks, or writes.");
                ImGui::Spacing();
                ImGui::TextWrapped("Allowed sources: /proc, /sys, root status checks.");
                ImGui::TextWrapped("Forbidden: ptrace, LD_PRELOAD, hooking, /proc/<pid>/mem read/write on other processes, injection, external memory writes.");
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

void releaseWindow() {
    if (g_Window) {
        ANativeWindow_release(g_Window);
        g_Window = nullptr;
    }
}

void shutdownImGui() {
    if (g_Initialized) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplAndroid_Shutdown();
        ImGui::DestroyContext();
        g_Initialized = false;
    }
}

}  // namespace

extern "C" {

__attribute__((visibility("default"))) JNIEXPORT void JNICALL
Java_com_android_support_ImGuiBridge_nativeOnSurfaceCreated(JNIEnv* env, jclass, jobject surface) {
    std::lock_guard<std::mutex> lock(g_FrameMutex);
    releaseWindow();
    if (surface) {
        g_Window = ANativeWindow_fromSurface(env, surface);
        LOGI("nativeOnSurfaceCreated window=%p", g_Window);
    } else {
        LOGE("nativeOnSurfaceCreated: surface null");
        return;
    }

    if (eglGetCurrentContext() == EGL_NO_CONTEXT) {
        LOGE("nativeOnSurfaceCreated: no EGL context");
        return;
    }

    if (!g_Initialized) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.Fonts->AddFontDefault();
        io.FontGlobalScale = 2.0f;
        ImGui::StyleColorsDark();
        ImGui::GetStyle().ScaleAllSizes(2.2f);

        ImGui_ImplAndroid_Init(g_Window);
        ImGui_ImplOpenGL3_Init("#version 300 es");
        ImGui_ImplOpenGL3_CreateDeviceObjects();

        g_SystemInfo = core::getSystemInfo();
        g_Selinux = core::getSelinuxStatus();
        g_ShowMenu = true;
        g_Initialized = true;
        appendLog("ImGui initialized (read-only).");
        LOGI("ImGui initialized");
    }
}

__attribute__((visibility("default"))) JNIEXPORT void JNICALL
Java_com_android_support_ImGuiBridge_nativeOnSurfaceChanged(JNIEnv*, jclass, jint width, jint height) {
    std::lock_guard<std::mutex> lock(g_FrameMutex);
    g_Width = width;
    g_Height = height;
    LOGI("nativeOnSurfaceChanged %dx%d", width, height);
    if (width <= 0 || height <= 0) return;
    glViewport(0, 0, width, height);
    if (g_Initialized) {
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)width, (float)height);
    }
}

__attribute__((visibility("default"))) JNIEXPORT void JNICALL
Java_com_android_support_ImGuiBridge_nativeOnDrawFrame(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> lock(g_FrameMutex);
    if (g_Width <= 0 || g_Height <= 0) return;
    if (eglGetCurrentContext() == EGL_NO_CONTEXT) return;
    if (!g_Initialized) return;

    glViewport(0, 0, g_Width, g_Height);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    float dt = ImGui::GetIO().DeltaTime;
    updateTelemetry(dt > 0.0f ? dt : (1.0f / 60.0f));

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame();
    ImGui::NewFrame();

    if (g_ShowMenu) {
        drawMenu();
    } else {
        ImGui::SetNextWindowPos(ImVec2(20.0f, 120.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(140.0f, 60.0f), ImGuiCond_Always);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
        if (ImGui::Begin("##OpenMenu", nullptr, flags)) {
            if (ImGui::Button("OPEN", ImVec2(-1.0f, -1.0f))) g_ShowMenu = true;
        }
        ImGui::End();
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

__attribute__((visibility("default"))) JNIEXPORT void JNICALL
Java_com_android_support_ImGuiBridge_nativeOnTouch(JNIEnv*, jclass, jfloat x, jfloat y, jint action) {
    std::lock_guard<std::mutex> lock(g_FrameMutex);
    if (!g_Initialized) return;
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(x, y);
    if (action == 0) io.AddMouseButtonEvent(0, true);
    else if (action == 1 || action == 3) io.AddMouseButtonEvent(0, false);
}

__attribute__((visibility("default"))) JNIEXPORT jboolean JNICALL
Java_com_android_support_ImGuiBridge_nativeIsMenuVisible(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> lock(g_FrameMutex);
    return g_ShowMenu ? JNI_TRUE : JNI_FALSE;
}

__attribute__((visibility("default"))) JNIEXPORT jboolean JNICALL
Java_com_android_support_ImGuiBridge_nativeWantCapture(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> lock(g_FrameMutex);
    if (!g_Initialized) return JNI_FALSE;
    return (g_ShowMenu && ImGui::GetIO().WantCaptureMouse) ? JNI_TRUE : JNI_FALSE;
}

__attribute__((visibility("default"))) JNIEXPORT void JNICALL
Java_com_android_support_ImGuiBridge_nativeSetRootStatus(JNIEnv* env, jclass, jstring status) {
    std::lock_guard<std::mutex> lock(g_FrameMutex);
    if (status == nullptr) return;
    const char* chars = env->GetStringUTFChars(status, nullptr);
    if (chars) {
        g_RootStatus = chars;
        env->ReleaseStringUTFChars(status, chars);
    }
}

__attribute__((visibility("default"))) JNIEXPORT void JNICALL
Java_com_android_support_ImGuiBridge_nativeOnSurfaceDestroyed(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> lock(g_FrameMutex);
    shutdownImGui();
    releaseWindow();
    g_Width = 0;
    g_Height = 0;
    LOGI("nativeOnSurfaceDestroyed");
}

__attribute__((visibility("default"))) JNIEXPORT jstring JNICALL
Java_com_android_support_NativeBridge_nativeGetSystemInfo(JNIEnv* env, jclass) {
    return env->NewStringUTF(core::getSystemInfo().c_str());
}

__attribute__((visibility("default"))) JNIEXPORT jstring JNICALL
Java_com_android_support_NativeBridge_nativeGetCpuLoad(JNIEnv* env, jclass) {
    return env->NewStringUTF(core::getCpuLoad().c_str());
}

}  // extern "C"
