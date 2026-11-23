#include <jni.h>
#include <pthread.h>
#include <android/input.h>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <sstream>
#include <iomanip>
#include <cctype>
#include <cstring>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>
#include <unistd.h>
#include <sys/system_properties.h>
#include <android/log.h>
#include <GLES3/gl3.h>
#include <EGL/egl.h>

#include "xdl.h"
#include "dobby.h"
#include "Include.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_android.h"
#include "Roboto-Regular.h"
#include "Icon.h"
#include "Iconcpp.h"
#include "Vector3.h"

#include "Il2Cpp/il2cpp_dump.h"

#include "include/Hook/KittyMemory/KittyMemory.h"
#include "include/Hook/KittyMemory/MemoryPatch.h"
#include "include/Hook/KittyMemory/KittyScanner.h"
#include "include/Hook/KittyMemory/KittyUtils.h"

#include "obfuscate.h"
#include "Utils.h"

std::vector<MemoryPatch> memoryPatches;
std::vector<uint64_t> offsetVector;

struct AntiDetectPatch {
    uint32_t rva;
    std::vector<uint8_t> original;
    std::vector<uint8_t> patched;
};

static const std::vector<AntiDetectPatch> kAntiDetectPatches = {
    {0x250FF8, {0xfd, 0x7b, 0xbe, 0xa9, 0xf4, 0x4f, 0x01, 0xa9},
               {0x1f, 0x00, 0x80, 0x52, 0xc0, 0x03, 0x5f, 0xd6}},
    {0x2B3DA0, {0x01, 0x02, 0x00, 0xb4, 0xfd, 0x7b, 0xbe, 0xa9},
               {0x1f, 0x00, 0x80, 0x52, 0xc0, 0x03, 0x5f, 0xd6}},
    {0x284420, {0xfd, 0x7b, 0xbe, 0xa9, 0xfc, 0x0b, 0x00, 0xf9},
               {0xc0, 0x5f, 0xd6, 0x03, 0x1f, 0x20, 0x03, 0xd5}},
    {0x2B3E08, {0xfd, 0x7b, 0xbe, 0xa9, 0xfc, 0x0b, 0x00, 0xf9},
               {0xc0, 0x5f, 0xd6, 0x03, 0x1f, 0x20, 0x03, 0xd5}},
    {0x24A10C, {0x88, 0x01, 0x00, 0x37}, {0x1f, 0x20, 0x03, 0xd5}},
    {0x25097C, {0xff, 0x83, 0x06, 0xd1, 0xfd, 0x7b, 0x17, 0xa9},
               {0x1f, 0x00, 0x80, 0x52, 0xc0, 0x03, 0x5f, 0xd6}},
    {0x25EAD8, {0xa0, 0x02, 0x00, 0x54}, {0x1f, 0x20, 0x03, 0xd5}},
    {0x25E9C8, {0x00, 0x74, 0x40, 0xb9, 0xc0, 0x03, 0x5f, 0xd6},
               {0x1f, 0x00, 0x80, 0x52, 0xc0, 0x03, 0x5f, 0xd6}},
    {0x25E9D0, {0x00, 0xe0, 0x41, 0x39, 0xc0, 0x03, 0x5f, 0xd6},
               {0x1f, 0x00, 0x80, 0x52, 0xc0, 0x03, 0x5f, 0xd6}},
    {0x2594AC, {0xfd, 0x7b, 0xbc, 0xa9, 0xfc, 0x0b, 0x00, 0xf9},
               {0xc0, 0x5f, 0xd6, 0x03, 0x1f, 0x20, 0x03, 0xd5}},
    {0x2570B8, {0x00, 0x04, 0x40, 0xf9, 0x60, 0x01, 0x00, 0xb4},
               {0x1f, 0x00, 0x80, 0x52, 0xc0, 0x03, 0x5f, 0xd6}},
    {0x256B2C, {0xfd, 0x7b, 0xbe, 0xa9, 0xfc, 0x0b, 0x00, 0xf9},
               {0xc0, 0x5f, 0xd6, 0x03, 0x1f, 0x20, 0x03, 0xd5}},
    {0x27A8DC, {0xfd, 0x7b, 0xbe, 0xa9, 0xfc, 0x4f, 0x01, 0xa9},
               {0xc0, 0x5f, 0xd6, 0x03, 0x1f, 0x20, 0x03, 0xd5}},
    {0x2D1700, {0xff, 0x03, 0x02, 0xd1, 0xfd, 0x7b, 0x06, 0xa9},
               {0xc0, 0x5f, 0xd6, 0x03, 0x1f, 0x20, 0x03, 0xd5}},
    {0x28ADC4, {0xff, 0x03, 0x02, 0xd1, 0xfd, 0x7b, 0x06, 0xa9},
               {0xc0, 0x5f, 0xd6, 0x03, 0x1f, 0x20, 0x03, 0xd5}},
};

static std::atomic<int> g_antidetect_applied{-1};
static pthread_mutex_t g_antidetect_mutex = PTHREAD_MUTEX_INITIALIZER;

using namespace ImGui;

std::string GetProp(const char* key) {
    char value[PROP_VALUE_MAX];
    __system_property_get(key, value);
    return std::string(value);
}

static uintptr_t findLibraryBias(const char *library) {
    char line[1024];
    FILE *fp = fopen("/proc/self/maps", "rt");
    if (!fp) return 0;

    while (fgets(line, sizeof(line), fp)) {
        unsigned long long start = 0;
        unsigned long long end = 0;
        unsigned long long offset = 0;
        char perms[8] = {0};
        if (sscanf(line, "%llx-%llx %7s %llx", &start, &end, perms, &offset) != 4) continue;
        if (offset != 0 || !strstr(line, library)) continue;
        fclose(fp);
        return static_cast<uintptr_t>(start);
    }

    fclose(fp);
    return 0;
}

static int applyAntiDetectPatches(uintptr_t bias) {
    int applied = 0;
    for (const auto &patch : kAntiDetectPatches) {
        uintptr_t address = bias + patch.rva;
        std::vector<uint8_t> current(patch.original.size());
        if (!KittyMemory::memRead(current.data(), reinterpret_cast<const void *>(address), current.size())) continue;
        if (current == patch.patched) {
            applied++;
            continue;
        }
        if (current != patch.original) continue;
        if (!KittyMemory::memWrite(reinterpret_cast<void *>(address), patch.patched.data(), patch.patched.size())) continue;

        std::vector<uint8_t> verify(patch.patched.size());
        if (KittyMemory::memRead(verify.data(), reinterpret_cast<const void *>(address), verify.size()) && verify == patch.patched) {
            applied++;
        }
    }
    return applied;
}

static void reapplyAntiDetect() {
    pthread_mutex_lock(&g_antidetect_mutex);
    uintptr_t bias = findLibraryBias(OBFUSCATE("libanogs.so"));
    if (!bias) {
        g_antidetect_applied.store(-1);
        pthread_mutex_unlock(&g_antidetect_mutex);
        return;
    }
    g_antidetect_applied.store(applyAntiDetectPatches(bias));
    pthread_mutex_unlock(&g_antidetect_mutex);
}

void *antidetect_thread(void *) {
    if (GetProp("meow.antidetect") != "1") return nullptr;
    int last = -2;
    for (;;) {
        reapplyAntiDetect();
        int applied = g_antidetect_applied.load();
        if (applied != last) {
            __android_log_print(ANDROID_LOG_INFO, "MEOW_ANTIDETECT", "applied=%d/%d", applied, static_cast<int>(kAntiDetectPatches.size()));
            last = applied;
        }
        sleep(1);
    }
    return nullptr;
}

int screenWidth;
int screenHeight;

int32_t (*orig_ANativeWindow_getWidth)(ANativeWindow* window);
int32_t _ANativeWindow_getWidth(ANativeWindow* window) {
	screenWidth = orig_ANativeWindow_getWidth(window);
	return orig_ANativeWindow_getWidth(window);
}

int32_t (*orig_ANativeWindow_getHeight)(ANativeWindow* window);
int32_t _ANativeWindow_getHeight(ANativeWindow* window) {
	screenHeight = orig_ANativeWindow_getHeight(window);
	return orig_ANativeWindow_getHeight(window);
}

bool esp = false;
bool drawEnemyBox = false;
bool noRecoil = false;
bool showMenu = true;
ImVec4 espLineColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
static float espMaxDistance = 999.0f;
static float linePositionX = 0.5f;
std::vector<Vector3> espPoints;

void (*setpitchyaw)(float, float);
Vector3 (*WorldToScreenPoint)(void*, Vector3, void*) = nullptr;
Vector3 (*get_position)(void*, void*) = nullptr;
void* (*get_main)(void*) = nullptr;
void* (*get_transform)(void*, void*) = nullptr;
void* (*CurrentLocalPlayer)(void*) = nullptr;
bool (*IsLocalTeammate)(void*, void*) = nullptr;
bool (*get_IsDead)(void*, void*) = nullptr;
void (*old_Player_update)(void* player, float a, float b, void* method);

struct ESPBox {
    Vector3 top;
    Vector3 bottom;
};
std::vector<ESPBox> espBoxes;
static std::mutex g_esp_mutex;
static std::recursive_mutex g_render_mutex;
static volatile bool g_il2cpp_ready = false;

// No recoil (Free Fire, offsets from dump.cs)
// Recoil.Apply:  void(float, float)   RVA 0x74BAA18
// Recoil.GetDelta: Vector2()           RVA 0x74BB99C
struct Vec2f { float x; float y; };
void (*orig_RecoilApply)(void*, float, float, void*) = nullptr;
Vec2f (*orig_RecoilGetDelta)(void*, void*) = nullptr;

void my_RecoilApply(void* thiz, float a, float b, void* method) {
    if (noRecoil) return;
    if (orig_RecoilApply) orig_RecoilApply(thiz, a, b, method);
}

Vec2f my_RecoilGetDelta(void* thiz, void* method) {
    if (noRecoil) return {0.0f, 0.0f};
    if (orig_RecoilGetDelta) return orig_RecoilGetDelta(thiz, method);
    return {0.0f, 0.0f};
}

void Player_update(void* player, float a, float b, void* method) {
    if (old_Player_update) old_Player_update(player, a, b, method);
    if (!esp || !player || !g_il2cpp_ready) return;
    if (!get_transform || !get_position || !get_main || !WorldToScreenPoint) return;

    if (CurrentLocalPlayer) {
        void* local = CurrentLocalPlayer(nullptr);
        if (local && local == player) return;
    }
    if (IsLocalTeammate && IsLocalTeammate(player, nullptr)) return;
    if (get_IsDead && get_IsDead(player, nullptr)) return;

    void* transform = get_transform(player, nullptr);
    if (!transform) return;
    void* mainCam = get_main(nullptr);
    if (!mainCam) return;

    Vector3 pos = get_position(transform, nullptr);
    Vector3 bottomPos = { pos.x, pos.y, pos.z };
    Vector3 topPos = { pos.x, pos.y + 1.6f, pos.z };

    Vector3 screenBottom = WorldToScreenPoint(mainCam, bottomPos, nullptr);
    Vector3 screenTop = WorldToScreenPoint(mainCam, topPos, nullptr);

    if (screenBottom.z >= 1.0f && screenTop.z >= 1.0f) {
        screenBottom.y = screenHeight - screenBottom.y;
        screenTop.y = screenHeight - screenTop.y;
        std::lock_guard<std::mutex> lock(g_esp_mutex);
        espBoxes.push_back({ screenTop, screenBottom });
    }
}

void DrawESP() {
    std::vector<ESPBox> boxes;
    {
        std::lock_guard<std::mutex> lock(g_esp_mutex);
        if (!esp) {
            espBoxes.clear();
            return;
        }
        boxes.swap(espBoxes);
    }
    if (boxes.empty()) return;

    ImU32 espLineColorU32 = ImGui::ColorConvertFloat4ToU32(espLineColor);
    ImVec2 screenTopLine = ImVec2(screenWidth * linePositionX, 0.0f);
    auto* draw = ImGui::GetBackgroundDrawList();

    for (const auto& box : boxes) {
        if (box.bottom.z < 1.0f || box.top.z < 1.0f) continue;

        float boxHeight = fabsf(box.bottom.y - box.top.y);
        float boxWidth = boxHeight * 0.6f;

        ImVec2 boxMin = ImVec2(box.top.x - (boxWidth / 2), box.top.y);
        ImVec2 boxMax = ImVec2(box.top.x + (boxWidth / 2), box.top.y + boxHeight);

        if (drawEnemyBox)
            draw->AddRect(boxMin, boxMax, espLineColorU32, 0.0f, 15, 1.5f);
			
        draw->AddLine(screenTopLine, ImVec2(box.top.x, box.top.y), espLineColorU32, 2.0f);
    }
}


void DrawMenu() {
    if (!showMenu) {
        ImGui::SetNextWindowBgAlpha(0.6f);
        ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Once);
        ImGui::Begin("##menu_toggle", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);
        if (ImGui::Button("MENU", ImVec2(140, 60))) {
            showMenu = true;
            __android_log_print(ANDROID_LOG_INFO, "MEOW_MENU", "open");
        }
        ImGui::End();
        return;
    }

    const ImVec2 window_size = ImVec2(500, 420);
    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Once);
    ImGui::SetNextWindowSize(window_size, ImGuiCond_Once);
    ImGui::Begin("IMGUI MODMENU", &showMenu,
                 ImGuiWindowFlags_NoBringToFrontOnFocus |
                 ImGuiWindowFlags_NoResize);

    static bool dragLogged = false;
    if (ImGui::IsMouseDragging(0) && ImGui::IsWindowHovered()) {
        if (!dragLogged) {
            ImVec2 pos = ImGui::GetWindowPos();
            __android_log_print(ANDROID_LOG_INFO, "MEOW_MENU", "drag pos=%.0f,%.0f", pos.x, pos.y);
            dragLogged = true;
        }
    } else {
        dragLogged = false;
    }

    ImGui::SeparatorText("Unity <ESP>");
    if (ImGui::Checkbox("Enable Esp", &esp)) {
        __android_log_print(ANDROID_LOG_INFO, "MEOW_MENU", "esp=%d", esp);
    }
    if (ImGui::Checkbox("Draw Enemy Box", &drawEnemyBox)) {
        __android_log_print(ANDROID_LOG_INFO, "MEOW_MENU", "box=%d", drawEnemyBox);
    }

    ImGui::SeparatorText("Weapon <Features>");
    if (ImGui::Checkbox("No Recoil", &noRecoil)) {
        __android_log_print(ANDROID_LOG_INFO, "MEOW_MENU", "recoil=%d", noRecoil);
    }

    ImGui::SeparatorText("Anti-Detect <libanogs>");
    int applied = g_antidetect_applied.load();
    int total = static_cast<int>(kAntiDetectPatches.size());
    if (applied < 0) {
        ImGui::Text("Waiting for libanogs.so");
    } else {
        ImGui::Text("Active patches: %d/%d", applied, total);
        if (applied < total) ImGui::Text("Some RVAs do not match this game build");
    }
    if (ImGui::Button("Reapply Now", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
        reapplyAntiDetect();
        __android_log_print(ANDROID_LOG_INFO, "MEOW_MENU", "reapply=%d", g_antidetect_applied.load());
    }

    ImGui::SeparatorText("il2cpp <Dumper>");
    if (ImGui::Button("Dump IL2CPP", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
        __android_log_print(ANDROID_LOG_INFO, "MEOW_MENU", "dump start");
        auto il2cpp_handle = dlopen("libil2cpp.so", 4);
        if (il2cpp_handle) {
            il2cpp_dump(il2cpp_handle);
            __android_log_print(ANDROID_LOG_INFO, "MEOW_MENU", "dump done");
        } else {
            __android_log_print(ANDROID_LOG_INFO, "MEOW_MENU", "dump no handle");
        }
    }

    ImGui::End();
}

struct UnityEngine_Vector2_Fields {
    float x;
    float y;
};

struct UnityEngine_Vector2_o {
    UnityEngine_Vector2_Fields fields;
};

enum TouchPhase {
    Began = 0,
    Moved = 1,
    Stationary = 2,
    Ended = 3,
    Canceled = 4
};

struct UnityEngine_Touch_Fields {
    int32_t m_FingerId;
    UnityEngine_Vector2_o m_Position;
    UnityEngine_Vector2_o m_RawPosition;
    UnityEngine_Vector2_o m_PositionDelta;
    float m_TimeDelta;
    int32_t m_TapCount;
    int32_t m_Phase;
    int32_t m_Type;
    float m_Pressure;
    float m_maximumPossiblePressure;
    float m_Radius;
    float m_fRadiusVariance;
    float m_AltitudeAngle;
    float m_AzimuthAngle;
};

static int (*g_screenWidth)(void*) = nullptr;
static int (*g_screenHeight)(void*) = nullptr;
static int (*g_touchCount)(void*) = nullptr;
static UnityEngine_Touch_Fields (*g_getTouch)(int, void*) = nullptr;

void SetupImgui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    if (g_screenWidth && g_screenHeight) {
        io.DisplaySize = ImVec2((float)g_screenWidth(nullptr), (float)g_screenHeight(nullptr));
    } else if (screenWidth > 0 && screenHeight > 0) {
        io.DisplaySize = ImVec2((float)screenWidth, (float)screenHeight);
    } else {
        io.DisplaySize = ImVec2(1080.0f, 2400.0f);
    }
	ImGui::StyleColorsDark();
	ImGuiStyle *style = &ImGui::GetStyle();
	
	style->Alpha = 1.0f;
	style->WindowTitleAlign = ImVec2(0.5, 0.5);
	style->PopupRounding = 3;
    style->WindowPadding = ImVec2(4, 4);
    style->FramePadding  = ImVec2(2, 2);
    style->ItemSpacing   = ImVec2(2, 2);
    style->ScrollbarSize = 17;
    style->WindowBorderSize = 1;
    style->ChildBorderSize  = 1;
    style->PopupBorderSize  = 3;
    style->FrameBorderSize  = 1;
    style->WindowRounding    = 3;
    style->ChildRounding     = 3;
    style->FrameRounding     = 3;
    style->ScrollbarRounding = 2;
    style->GrabRounding      = 3;
    ImGui_ImplOpenGL3_Init("#version 100");
	
    ImFontConfig font_cfg;
    io.Fonts->AddFontFromMemoryTTF(&Roboto_Regular, sizeof(Roboto_Regular), 24.0f, &font_cfg, io.Fonts->GetGlyphRangesCyrillic());
    ImGui::GetStyle().ScaleAllSizes(1.8f);
}


EGLBoolean (*old_eglSwapBuffers)(EGLDisplay dpy, EGLSurface surface);

static EGLBoolean callOldSwap(EGLDisplay dpy, EGLSurface surface) {
    return old_eglSwapBuffers ? old_eglSwapBuffers(dpy, surface) : EGL_FALSE;
}

EGLBoolean hook_eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
    if (!g_il2cpp_ready) {
        return callOldSwap(dpy, surface);
    }

    std::lock_guard<std::recursive_mutex> renderLock(g_render_mutex);
    static bool is_setup = false;
    static bool should_clear_mouse_pos = false;

    if (!is_setup) {
		SetupImgui();
        is_setup = true;
    }

    ImGuiIO &io = ImGui::GetIO();
    if (g_screenWidth && g_screenHeight) {
        io.DisplaySize = ImVec2((float)g_screenWidth(nullptr), (float)g_screenHeight(nullptr));
    }
    if (g_touchCount && g_getTouch) {
        int touchCount = g_touchCount(nullptr);
        if (touchCount > 0) {
            UnityEngine_Touch_Fields touch = g_getTouch(0, nullptr);
            float reverseY = io.DisplaySize.y - touch.m_Position.fields.y;

            if (touch.m_Phase == TouchPhase::Began) {
                __android_log_print(ANDROID_LOG_INFO, "MEOW_INPUT",
                                    "display=%.0fx%.0f touch=%.1f,%.1f imgui=%.1f,%.1f",
                                    io.DisplaySize.x, io.DisplaySize.y,
                                    touch.m_Position.fields.x, touch.m_Position.fields.y,
                                    touch.m_Position.fields.x, reverseY);
            }

            switch (touch.m_Phase) {
                case TouchPhase::Began:
                case TouchPhase::Stationary:
                    io.MousePos = ImVec2(touch.m_Position.fields.x, reverseY);
                    io.MouseDown[0] = true;
                    break;
                case TouchPhase::Ended:
                case TouchPhase::Canceled:
                    io.MouseDown[0] = false;
                    should_clear_mouse_pos = true;
                    break;
                case TouchPhase::Moved:
                    io.MousePos = ImVec2(touch.m_Position.fields.x, reverseY);
                    break;
                default:
                    break;
            }
        } else {
            io.MouseDown[0] = false;
        }
    }
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
	DrawESP();
	DrawMenu();
	ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    ImGui::EndFrame();
    if (should_clear_mouse_pos) {
        io.MousePos = ImVec2(-1, -1);
        should_clear_mouse_pos = false;
    }
    return callOldSwap(dpy, surface);
}


static void *resolveAndroidSymbol(const char *name) {
    void *symbol = DobbySymbolResolver(OBFUSCATE("/system/lib64/libandroid.so"), name);
    if (!symbol) symbol = DobbySymbolResolver(OBFUSCATE("/system/lib/libandroid.so"), name);
    return symbol;
}

void *imgui_go(void*) {
    if (GetProp("meow.overlay") == "0") return nullptr;
    void *handle_egl = xdl_open("libEGL.so", XDL_DEFAULT);
    void *egl_swap = handle_egl ? xdl_sym(handle_egl, "eglSwapBuffers", nullptr) : nullptr;
    if (egl_swap) {
        DobbyHook(egl_swap, (void*)hook_eglSwapBuffers, (void**)&old_eglSwapBuffers);
    }

    void *get_width = resolveAndroidSymbol(OBFUSCATE("ANativeWindow_getWidth"));
    void *get_height = resolveAndroidSymbol(OBFUSCATE("ANativeWindow_getHeight"));
    if (get_width) {
        DobbyHook(get_width, (void *) _ANativeWindow_getWidth, (void **) &orig_ANativeWindow_getWidth);
    }
    if (get_height) {
        DobbyHook(get_height, (void *) _ANativeWindow_getHeight, (void **) &orig_ANativeWindow_getHeight);
    }

    __android_log_print(ANDROID_LOG_INFO, "MEOW_INPUT", "egl=%d width=%d height=%d", egl_swap != nullptr, get_width != nullptr, get_height != nullptr);
    pthread_exit(nullptr);
}

void *hack_thread(void*) {
    if (GetProp("meow.il2cpp") == "0") return nullptr;
	do {
     sleep(1);
    } while (!isLibraryLoaded("libil2cpp.so"));
	
    // Offsets from dump.cs
    WorldToScreenPoint = (Vector3(*)(void*, Vector3, void*)) getAbsoluteAddress("libil2cpp.so", 0x9F8F984);
    get_position = (Vector3 (*)(void*, void*)) getAbsoluteAddress("libil2cpp.so", 0x9FE5A68);
    get_transform = (void *(*)(void*, void*)) getAbsoluteAddress("libil2cpp.so", 0x9FD75D0);
    get_main = (void*(*)(void*)) getAbsoluteAddress("libil2cpp.so", 0x9F8FD00);
    g_screenWidth = (int (*)(void*)) getAbsoluteAddress("libil2cpp.so", 0x9F9D1D4);
    g_screenHeight = (int (*)(void*)) getAbsoluteAddress("libil2cpp.so", 0x9F9D1FC);
    g_touchCount = (int (*)(void*)) getAbsoluteAddress("libil2cpp.so", 0xA018CA0);
    g_getTouch = (UnityEngine_Touch_Fields (*)(int, void*)) getAbsoluteAddress("libil2cpp.so", 0xA0186C8);
    CurrentLocalPlayer = (void* (*)(void*)) getAbsoluteAddress("libil2cpp.so", 0x71BD10C); // GameFacade.CurrentLocalPlayer()
    IsLocalTeammate = (bool (*)(void*, void*)) getAbsoluteAddress("libil2cpp.so", 0x71BD8EC); // GameFacade.IsLocalTeammate(Player)
    get_IsDead = (bool (*)(void*, void*)) getAbsoluteAddress("libil2cpp.so", 0x716C210); // Player.get_IsDead()

    g_il2cpp_ready = true;
    sleep(5);

	return nullptr;
}

__attribute__((constructor))
void lib_main() {
    pthread_t hacks;
    pthread_create(&hacks, NULL, antidetect_thread, NULL);
    pthread_create(&hacks, NULL, imgui_go, NULL);
    pthread_create(&hacks, NULL, hack_thread, NULL);
}

extern "C" jint JNIEXPORT JNI_OnLoad(JavaVM *vm, void *key) {
     jvm = vm;
    JNIEnv *env;
    vm->GetEnv((void**)&env, JNI_VERSION_1_6);
    return JNI_VERSION_1_6;
}
