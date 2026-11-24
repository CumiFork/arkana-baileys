#include <jni.h>
#include <android/native_window_jni.h>
#include <android/log.h>
#include <GLES3/gl3.h>
#include <mutex>
#include <cinttypes>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "menu.h"
#include "feature_state.h"
#include "external_engine.h"

#define LOG_TAG "MEOW_V2EXT"

namespace {
std::mutex g_mu;
bool g_imguiReady = false;
int g_w = 0, g_h = 0;
bool g_wantCapture = false;
std::string g_rootStatus = "unknown";

void ensureInit() {
    if (g_imguiReady) return;
    v2::LoadConfig();
    v2menu::SetupImGui();
    ext::Start();
    g_imguiReady = true;
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "APK bridge init (external mode)");
}

void setStatusInInfo() {
    std::lock_guard<std::mutex> lk(v2::InfoMutex());
    auto& i = v2::Info();
    if (i.hookDetail.empty()) i.hookDetail = "external mode";
}

}  // namespace

extern "C" {

JNIEXPORT void JNICALL
Java_com_android_support_ImGuiBridge_nativeOnSurfaceCreated(JNIEnv* env, jclass, jobject /*surface*/) {
    std::lock_guard<std::mutex> lk(g_mu);
    (void)env;
    ensureInit();
    setStatusInInfo();
}

JNIEXPORT void JNICALL
Java_com_android_support_ImGuiBridge_nativeOnSurfaceChanged(JNIEnv*, jclass, jint w, jint h) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_w = w; g_h = h;
    glViewport(0, 0, w, h);
}

JNIEXPORT void JNICALL
Java_com_android_support_ImGuiBridge_nativeOnDrawFrame(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_imguiReady) ensureInit();
    if (g_w <= 0 || g_h <= 0) return;

    ext::FrameData f = ext::Snapshot();
    f.width = g_w; f.height = g_h;
    // store screen size for W2S consumers via Info? W2S uses FrameData passed; DrawESP in menu uses game::WorldToScreen (in-process). 
    // For external APK mode, we render ESP from external snapshot.
    {
        std::lock_guard<std::mutex> lk2(v2::InfoMutex());
        auto& i = v2::Info();
        i.hookState = f.status;
        i.hookDetail = f.detail;
        i.battleManager = f.battleManager;
        i.localPlayer = f.localPlayer;
        i.localCamp = f.localCamp;
        i.localRank = f.localRank;
        i.roomId = f.roomId;
        i.playerCount = f.playerCount;
        i.enemyCount = f.enemyCount;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)g_w, (float)g_h);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    v2menu::DrawExternalESP(f);
    v2menu::DrawMenu();

    ImGui::Render();
    glViewport(0, 0, g_w, g_h);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    ImGui::EndFrame();
    g_wantCapture = ImGui::GetIO().WantCaptureMouse;
}

JNIEXPORT void JNICALL
Java_com_android_support_ImGuiBridge_nativeOnTouch(JNIEnv*, jclass, jfloat x, jfloat y, jint action) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_imguiReady) return;
    ImGuiIO& io = ImGui::GetIO();
    io.MousePos = ImVec2(x, y);
    // ACTION_DOWN=0, UP=1, MOVE=2, CANCEL=3
    if (action == 0 || action == 2) io.MouseDown[0] = true;
    else if (action == 1 || action == 3) io.MouseDown[0] = false;
}

JNIEXPORT jboolean JNICALL
Java_com_android_support_ImGuiBridge_nativeIsMenuVisible(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> lk(v2::StateMutex());
    return v2::State().menu.open ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_android_support_ImGuiBridge_nativeSetMenuVisible(JNIEnv*, jclass, jboolean visible) {
    std::lock_guard<std::mutex> lk(v2::StateMutex());
    v2::State().menu.open = (visible == JNI_TRUE);
}

JNIEXPORT jboolean JNICALL
Java_com_android_support_ImGuiBridge_nativeWantCapture(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_wantCapture ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_android_support_ImGuiBridge_nativeSetRootStatus(JNIEnv* env, jclass, jstring status) {
    if (!status) return;
    const char* s = env->GetStringUTFChars(status, nullptr);
    if (s) {
        std::lock_guard<std::mutex> lk(g_mu);
        g_rootStatus = s;
        std::lock_guard<std::mutex> lk2(v2::InfoMutex());
        v2::Info().hookDetail = std::string("external mode | root: ") + g_rootStatus;
    }
    env->ReleaseStringUTFChars(status, s);
}

JNIEXPORT void JNICALL
Java_com_android_support_ImGuiBridge_nativeOnSurfaceDestroyed(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> lk(g_mu);
    // keep engine running; ImGui context persists
}

JNIEXPORT jstring JNICALL
Java_com_android_support_NativeBridge_nativeGetSystemInfo(JNIEnv* env, jclass) {
    ext::FrameData f = ext::Snapshot();
    char buf[512];
    snprintf(buf, sizeof(buf), "External ESP mode\nStatus: %s\n%s\nBM: 0x%" PRIxPTR " Players: %d Enemies: %d\nVMatrix: %s",
             f.status.c_str(), f.detail.c_str(), f.battleManager, f.playerCount, f.enemyCount, f.hasMatrix ? "OK" : "not resolved");
    return env->NewStringUTF(buf);
}

JNIEXPORT jstring JNICALL
Java_com_android_support_NativeBridge_nativeGetCpuLoad(JNIEnv* env, jclass) {
    ext::FrameData f = ext::Snapshot();
    char buf[256];
    snprintf(buf, sizeof(buf), "Status: %s\nVMatrix: %s\nPlayers: %d",
             f.status.c_str(), f.hasMatrix ? "OK" : "no", f.playerCount);
    return env->NewStringUTF(buf);
}

}  // extern "C"
