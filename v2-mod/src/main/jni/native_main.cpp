#include <android/log.h>
#include <android/native_window.h>
#include <android/looper.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/system_properties.h>
#include <unistd.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "menu.h"
#include "feature_state.h"
#include "external_engine.h"
#include "native_surface.h"

#define LOG_TAG "MEOW_V2NATIVE"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace {

std::atomic<bool> g_running{true};
std::atomic<int> g_screenW{0};
std::atomic<int> g_screenH{0};
std::atomic<int> g_rot{0}; // 0,1,2,3
std::mutex g_inputMu;
ImVec2 g_touchPos{0, 0};
bool g_touchDown = false;

void onSignal(int) {
    g_running.store(false);
    v2::RequestExit();
}

int getPropInt(const char* key, int def) {
    char v[PROP_VALUE_MAX] = {0};
    if (__system_property_get(key, v) > 0) return atoi(v);
    return def;
}

void updateRotationAndSize() {
    // Read rotation from system property; fallback to display size
    int rot = getPropInt("service.sf.primary_display_orientation", -1);
    if (rot < 0) rot = getPropInt("ro.surface_flinger.primary_display_orientation", -1);
    // common: ORIENTATION_0/90/180/270 mapped differently; keep as 0-3 if small
    if (rot >= 0 && rot <= 3) g_rot.store(rot);
    // screen size will be set by EGL/native window query
}

struct TouchSlot {
    int trackingId = -1;
    float x = 0;
    float y = 0;
};

struct InputDevice {
    int fd = -1;
    int minX = 0, maxX = 0, minY = 0, maxY = 0;
    int slot = 0;
    TouchSlot slots[10];
};

std::vector<InputDevice> g_devs;

bool openInputDevices() {
    g_devs.clear();
    DIR* dir = opendir("/dev/input");
    if (!dir) { LOGE("open /dev/input failed: %s", strerror(errno)); return false; }
    struct dirent* de;
    while ((de = readdir(dir)) != nullptr) {
        if (strncmp(de->d_name, "event", 5) != 0) continue;
        std::string path = std::string("/dev/input/") + de->d_name;
        int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        // check for ABS_MT_POSITION_X/Y or ABS_X/Y
        unsigned long absBits[(ABS_MAX + 7) / 8 + 1] = {0};
        ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absBits)), absBits);
        auto hasBit = [&](int bit) { return (absBits[bit / 8] & (1u << (bit % 8))) != 0; };
        bool hasTouch = hasBit(ABS_MT_POSITION_X) && hasBit(ABS_MT_POSITION_Y);
        bool hasAbs = hasBit(ABS_X) && hasBit(ABS_Y);
        if (!hasTouch && !hasAbs) { close(fd); continue; }
        InputDevice dev;
        dev.fd = fd;
        struct input_absinfo ax{}, ay{};
        int xCode = hasTouch ? ABS_MT_POSITION_X : ABS_X;
        int yCode = hasTouch ? ABS_MT_POSITION_Y : ABS_Y;
        if (ioctl(fd, EVIOCGABS(xCode), &ax) == 0) { dev.minX = ax.minimum; dev.maxX = ax.maximum; }
        if (ioctl(fd, EVIOCGABS(yCode), &ay) == 0) { dev.minY = ay.minimum; dev.maxY = ay.maximum; }
        for (auto& s : dev.slots) s.trackingId = -1;
        g_devs.push_back(dev);
        LOGI("input device: %s x[%d,%d] y[%d,%d] mt=%d", path.c_str(), dev.minX, dev.maxX, dev.minY, dev.maxY, hasTouch);
    }
    closedir(dir);
    return !g_devs.empty();
}

void mapToScreen(const InputDevice& dev, float rx, float ry, float& sx, float& sy) {
    float w = (float)g_screenW.load();
    float h = (float)g_screenH.load();
    if (w <= 0 || h <= 0) { sx = rx; sy = ry; return; }
    float nx = (dev.maxX > dev.minX) ? (rx - dev.minX) / (float)(dev.maxX - dev.minX) : 0.0f;
    float ny = (dev.maxY > dev.minY) ? (ry - dev.minY) / (float)(dev.maxY - dev.minY) : 0.0f;
    if (nx < 0) nx = 0; if (nx > 1) nx = 1;
    if (ny < 0) ny = 0; if (ny > 1) ny = 1;
    int rot = g_rot.load();
    // Map normalized touch to current screen orientation
    switch (rot) {
        case 1: sx = ny * w; sy = (1.0f - nx) * h; break;
        case 2: sx = (1.0f - nx) * w; sy = (1.0f - ny) * h; break;
        case 3: sx = (1.0f - ny) * w; sy = nx * h; break;
        default: sx = nx * w; sy = ny * h; break;
    }
}

void inputThread() {
    if (!openInputDevices()) {
        LOGW("no touch input devices found");
    }
    std::vector<pollfd> pfds;
    while (g_running.load() && !v2::ExitRequested().load()) {
        pfds.clear();
        for (auto& d : g_devs) { pollfd p{}; p.fd = d.fd; p.events = POLLIN; pfds.push_back(p); }
        if (pfds.empty()) { std::this_thread::sleep_for(std::chrono::milliseconds(500)); openInputDevices(); continue; }
        int r = poll(pfds.data(), pfds.size(), 100);
        if (r <= 0) continue;
        for (size_t i = 0; i < pfds.size() && i < g_devs.size(); ++i) {
            if (!(pfds[i].revents & POLLIN)) continue;
            InputDevice& dev = g_devs[i];
            struct input_event ev;
            while (read(dev.fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
                if (ev.type == EV_ABS) {
                    if (ev.code == ABS_MT_SLOT) dev.slot = std::max(0, std::min(9, ev.value));
                    else if (ev.code == ABS_MT_TRACKING_ID) dev.slots[dev.slot].trackingId = ev.value;
                    else if (ev.code == ABS_MT_POSITION_X || ev.code == ABS_X) dev.slots[dev.slot].x = (float)ev.value;
                    else if (ev.code == ABS_MT_POSITION_Y || ev.code == ABS_Y) dev.slots[dev.slot].y = (float)ev.value;
                } else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
                    // pick first active slot
                    bool down = false; float sx=0, sy=0;
                    for (auto& s : dev.slots) {
                        if (s.trackingId >= 0) { mapToScreen(dev, s.x, s.y, sx, sy); down = true; break; }
                    }
                    std::lock_guard<std::mutex> lk(g_inputMu);
                    g_touchDown = down;
                    if (down) { g_touchPos = ImVec2(sx, sy); }
                }
            }
        }
    }
}

// ---------- Native overlay ----------

struct OverlaySurface {
    NativeOverlay overlay{};
    EGLDisplay eglDisplay = EGL_NO_DISPLAY;
    EGLSurface eglSurface = EGL_NO_SURFACE;
    EGLContext eglContext = EGL_NO_CONTEXT;
    int w = 0, h = 0;
};

int getDisplaySize(int& w, int& h) {
    w = getPropInt("debug.force.display.width", 0);
    h = getPropInt("debug.force.display.height", 0);
    if (w > 0 && h > 0) return 0;
    FILE* fp = popen("wm size 2>/dev/null | tr -cd '0-9x\\n' | tail -n1", "r");
    if (fp) {
        char buf[64] = {0};
        if (fgets(buf, sizeof(buf), fp)) {
            int rw=0,rh=0;
            if (sscanf(buf, "%dx%d", &rw, &rh) == 2 && rw>0 && rh>0) { w=rw; h=rh; }
        }
        pclose(fp);
    }
    if (w <= 0 || h <= 0) { w = 1080; h = 2400; }
    return 0;
}

bool createOverlayEGL(OverlaySurface& out) {
    getDisplaySize(out.w, out.h);
    g_screenW.store(out.w); g_screenH.store(out.h);
    LOGI("overlay target size %dx%d", out.w, out.h);

    if (!NativeOverlay_Create(&out.overlay, out.w, out.h) || !out.overlay.window) {
        LOGE("NativeOverlay_Create failed");
        return false;
    }
    return true;
}

bool initEGLWindow(OverlaySurface& out) {
    out.eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (out.eglDisplay == EGL_NO_DISPLAY) { LOGE("eglGetDisplay failed"); return false; }
    if (!eglInitialize(out.eglDisplay, nullptr, nullptr)) { LOGE("eglInitialize failed"); return false; }
    const EGLint cfgAttrs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_BLUE_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_RED_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 16, EGL_STENCIL_SIZE, 8,
        EGL_NONE
    };
    EGLConfig cfg; EGLint n=0;
    if (!eglChooseConfig(out.eglDisplay, cfgAttrs, &cfg, 1, &n) || n <= 0) { LOGE("eglChooseConfig failed"); return false; }

    EGLint fmt = 0;
    eglGetConfigAttrib(out.eglDisplay, cfg, EGL_NATIVE_VISUAL_ID, &fmt);
    ANativeWindow_setBuffersGeometry(out.overlay.window, out.w, out.h, fmt);

    out.eglSurface = eglCreateWindowSurface(out.eglDisplay, cfg, out.overlay.window, nullptr);
    if (out.eglSurface == EGL_NO_SURFACE) { LOGE("eglCreateWindowSurface failed: 0x%x", eglGetError()); return false; }
    const EGLint ctxAttrs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    out.eglContext = eglCreateContext(out.eglDisplay, cfg, EGL_NO_CONTEXT, ctxAttrs);
    if (out.eglContext == EGL_NO_CONTEXT) { LOGE("eglCreateContext failed: 0x%x", eglGetError()); return false; }
    if (!eglMakeCurrent(out.eglDisplay, out.eglSurface, out.eglSurface, out.eglContext)) { LOGE("eglMakeCurrent failed"); return false; }
    eglSwapInterval(out.eglDisplay, 1);
    eglQuerySurface(out.eglDisplay, out.eglSurface, EGL_WIDTH, &out.w);
    eglQuerySurface(out.eglDisplay, out.eglSurface, EGL_HEIGHT, &out.h);
    if (out.w > 0 && out.h > 0) { g_screenW.store(out.w); g_screenH.store(out.h); }
    LOGI("EGL ready %dx%d", out.w, out.h);
    return true;
}

void destroyOverlay(OverlaySurface& out) {
    if (out.eglDisplay != EGL_NO_DISPLAY) {
        eglMakeCurrent(out.eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (out.eglContext != EGL_NO_CONTEXT) eglDestroyContext(out.eglDisplay, out.eglContext);
        if (out.eglSurface != EGL_NO_SURFACE) eglDestroySurface(out.eglDisplay, out.eglSurface);
        eglTerminate(out.eglDisplay);
    }
    out.eglDisplay = EGL_NO_DISPLAY; out.eglContext = EGL_NO_CONTEXT; out.eglSurface = EGL_NO_SURFACE;
    NativeOverlay_Destroy(&out.overlay);
}

}  // namespace

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);
    LOGI("v2-mod native overlay starting");

    if (geteuid() != 0) {
        LOGE("Root required. Re-run with: su -c /data/local/tmp/v2mod");
        fprintf(stderr, "Root required. Run with su -c\n");
        return 1;
    }

    v2::LoadConfig();
    v2::WriteStatus();
    ext::Start();

    std::thread inThr(inputThread);

    bool imguiSetup = false;
    while (g_running.load() && !v2::ExitRequested().load()) {
        OverlaySurface ov{};
        if (!createOverlayEGL(ov)) {
            LOGE("Overlay creation failed");
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }
        if (!initEGLWindow(ov)) {
            destroyOverlay(ov);
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }
        if (!imguiSetup) {
            v2menu::SetupImGui();
            imguiSetup = true;
        } else {
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplOpenGL3_Init("#version 300 es");
        }

        int frameCount = 0;
        while (g_running.load() && !v2::ExitRequested().load()) {
        eglQuerySurface(ov.eglDisplay, ov.eglSurface, EGL_WIDTH, &ov.w);
        eglQuerySurface(ov.eglDisplay, ov.eglSurface, EGL_HEIGHT, &ov.h);
        if (ov.w > 0 && ov.h > 0) { g_screenW.store(ov.w); g_screenH.store(ov.h); }
        if ((++frameCount % 120) == 0) {
            int dw=0, dh=0; getDisplaySize(dw, dh);
            if (dw > 0 && dh > 0 && (dw != ov.overlay.width || dh != ov.overlay.height)) {
                LOGI("Display size changed %dx%d -> %dx%d, recreating overlay", ov.overlay.width, ov.overlay.height, dw, dh);
                break; // recreate in outer loop
            }
        }
        updateRotationAndSize();

        glViewport(0, 0, ov.w, ov.h);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)ov.w, (float)ov.h);
        {
            std::lock_guard<std::mutex> lk(g_inputMu);
            io.MousePos = g_touchPos;
            io.MouseDown[0] = g_touchDown;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();

        ext::FrameData f = ext::Snapshot();
        f.width = ov.w; f.height = ov.h;
        {
            std::lock_guard<std::mutex> lk(v2::InfoMutex());
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

        v2menu::DrawExternalESP(f);
        v2menu::DrawMenu();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        ImGui::EndFrame();
        eglSwapBuffers(ov.eglDisplay, ov.eglSurface);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    destroyOverlay(ov);
    if (v2::ExitRequested().load() || !g_running.load()) break;
        // size changed; recreate overlay
    }

    g_running.store(false);
    if (inThr.joinable()) inThr.join();
    ext::Stop();
    LOGI("v2-mod native overlay exited");
    return 0;
}
