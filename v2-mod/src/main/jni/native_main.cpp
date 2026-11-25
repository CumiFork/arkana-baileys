#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <dirent.h>
#include <fcntl.h>
#include <jni.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/system_properties.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <chrono>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "menu.h"
#include "feature_state.h"
#include "external_engine.h"

#define LOG_TAG "MEOW_V2NATIVE"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace {

std::atomic<bool> g_running{true};
std::atomic<int> g_screenW{0};
std::atomic<int> g_screenH{0};
std::atomic<int> g_rot{0};
std::mutex g_inputMu;
ImVec2 g_touchPos{0, 0};
bool g_touchDown = false;

void onSignal(int) {
    g_running.store(false);
    v2::RequestExit();
}

int getPropInt(const char* key, int def) {
    char v[PROP_VALUE_MAX] = {0};
    int len = __system_property_get(key, v);
    if (len > 0) return atoi(v);
    return def;
}

void updateRotationAndSize() {
    int rot = getPropInt("service.sf.primary_display_orientation", -1);
    if (rot < 0) rot = getPropInt("ro.surface_flinger.primary_display_orientation", -1);
    if (rot >= 0 && rot <= 3) g_rot.store(rot);
}

struct TouchSlot {
    int trackingId = -1;
    float x = 0.0f;
    float y = 0.0f;
};

struct InputDevice {
    int fd = -1;
    int minX = 0;
    int maxX = 0;
    int minY = 0;
    int maxY = 0;
    int slot = 0;
    TouchSlot slots[10];
};

std::vector<InputDevice> g_devs;
std::mutex g_devsMu;

void closeInputDevicesLocked() {
    for (auto& d : g_devs) {
        if (d.fd >= 0) {
            close(d.fd);
            d.fd = -1;
        }
    }
    g_devs.clear();
}

bool openInputDevices() {
    std::lock_guard<std::mutex> lk(g_devsMu);
    closeInputDevicesLocked();
    DIR* dir = opendir("/dev/input");
    if (!dir) {
        LOGE("open /dev/input failed: %s", strerror(errno));
        return false;
    }
    struct dirent* de;
    while ((de = readdir(dir)) != nullptr) {
        if (strncmp(de->d_name, "event", 5) != 0) continue;
        char path[64] = {0};
        snprintf(path, sizeof(path), "/dev/input/%s", de->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;

        unsigned long absBits[(ABS_MAX / 8) + 1] = {0};
        if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absBits)), absBits) < 0) {
            close(fd);
            continue;
        }
        auto hasBit = [&](int bit) -> bool {
            if (bit < 0 || bit > ABS_MAX) return false;
            return (absBits[bit / 8] & (1u << (bit % 8))) != 0;
        };
        bool hasTouch = hasBit(ABS_MT_POSITION_X) && hasBit(ABS_MT_POSITION_Y);
        bool hasAbs = hasBit(ABS_X) && hasBit(ABS_Y);
        if (!hasTouch && !hasAbs) {
            close(fd);
            continue;
        }

        InputDevice dev;
        dev.fd = fd;
        struct input_absinfo ax{};
        struct input_absinfo ay{};
        int xCode = hasTouch ? ABS_MT_POSITION_X : ABS_X;
        int yCode = hasTouch ? ABS_MT_POSITION_Y : ABS_Y;
        if (ioctl(fd, EVIOCGABS(xCode), &ax) == 0) {
            dev.minX = ax.minimum;
            dev.maxX = ax.maximum;
        }
        if (ioctl(fd, EVIOCGABS(yCode), &ay) == 0) {
            dev.minY = ay.minimum;
            dev.maxY = ay.maximum;
        }
        for (auto& s : dev.slots) {
            s.trackingId = -1;
            s.x = 0.0f;
            s.y = 0.0f;
        }
        g_devs.push_back(dev);
        LOGI("input device: %s x[%d,%d] y[%d,%d] mt=%d", path, dev.minX, dev.maxX, dev.minY, dev.maxY, hasTouch ? 1 : 0);
    }
    closedir(dir);
    return !g_devs.empty();
}

void mapToScreen(const InputDevice& dev, float rx, float ry, float& sx, float& sy) {
    float w = static_cast<float>(g_screenW.load());
    float h = static_cast<float>(g_screenH.load());
    if (w <= 0.0f || h <= 0.0f) {
        sx = rx;
        sy = ry;
        return;
    }
    float nx = (dev.maxX > dev.minX) ? (rx - static_cast<float>(dev.minX)) / static_cast<float>(dev.maxX - dev.minX) : 0.0f;
    float ny = (dev.maxY > dev.minY) ? (ry - static_cast<float>(dev.minY)) / static_cast<float>(dev.maxY - dev.minY) : 0.0f;
    nx = std::clamp(nx, 0.0f, 1.0f);
    ny = std::clamp(ny, 0.0f, 1.0f);
    int rot = g_rot.load();
    switch (rot) {
        case 1: sx = ny * w; sy = (1.0f - nx) * h; break;
        case 2: sx = (1.0f - nx) * w; sy = (1.0f - ny) * h; break;
        case 3: sx = (1.0f - ny) * w; sy = nx * h; break;
        default: sx = nx * w; sy = ny * h; break;
    }
}

void inputThread() {
    if (!openInputDevices()) {
        LOGW("no touch input devices found at startup");
    }
    std::vector<pollfd> pfds;
    std::vector<int> fdIndex;
    while (g_running.load() && !v2::ExitRequested().load()) {
        pfds.clear();
        fdIndex.clear();
        {
            std::lock_guard<std::mutex> lk(g_devsMu);
            for (size_t i = 0; i < g_devs.size(); ++i) {
                if (g_devs[i].fd < 0) continue;
                pollfd p{};
                p.fd = g_devs[i].fd;
                p.events = POLLIN;
                p.revents = 0;
                pfds.push_back(p);
                fdIndex.push_back(static_cast<int>(i));
            }
        }
        if (pfds.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            if (g_running.load() && !v2::ExitRequested().load()) openInputDevices();
            continue;
        }

        int r = poll(pfds.data(), static_cast<nfds_t>(pfds.size()), 100);
        if (r < 0) {
            if (errno == EINTR) continue;
            LOGW("poll failed: %s", strerror(errno));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        if (r == 0) continue;

        for (size_t i = 0; i < pfds.size(); ++i) {
            if (!(pfds[i].revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL))) continue;
            if (pfds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) continue;
            int devIdx = fdIndex[i];
            InputDevice* devPtr = nullptr;
            {
                std::lock_guard<std::mutex> lk(g_devsMu);
                if (devIdx < 0 || devIdx >= static_cast<int>(g_devs.size())) continue;
                devPtr = &g_devs[static_cast<size_t>(devIdx)];
                if (devPtr->fd != pfds[i].fd || devPtr->fd < 0) continue;
            }

            struct input_event evs[64];
            ssize_t n = read(pfds[i].fd, evs, sizeof(evs));
            if (n < 0) {
                if (errno == EAGAIN || errno == EINTR) continue;
                continue;
            }
            int count = static_cast<int>(n / static_cast<ssize_t>(sizeof(struct input_event)));
            if (count <= 0) continue;

            std::lock_guard<std::mutex> lk(g_devsMu);
            if (devIdx < 0 || devIdx >= static_cast<int>(g_devs.size())) continue;
            InputDevice& dev = g_devs[static_cast<size_t>(devIdx)];
            if (dev.fd != pfds[i].fd || dev.fd < 0) continue;

            for (int e = 0; e < count; ++e) {
                const struct input_event& ev = evs[e];
                if (ev.type == EV_ABS) {
                    if (ev.code == ABS_MT_SLOT) {
                        dev.slot = std::clamp(ev.value, 0, 9);
                    } else if (ev.code == ABS_MT_TRACKING_ID) {
                        dev.slots[dev.slot].trackingId = ev.value;
                    } else if (ev.code == ABS_MT_POSITION_X || ev.code == ABS_X) {
                        dev.slots[dev.slot].x = static_cast<float>(ev.value);
                    } else if (ev.code == ABS_MT_POSITION_Y || ev.code == ABS_Y) {
                        dev.slots[dev.slot].y = static_cast<float>(ev.value);
                    }
                } else if (ev.type == EV_KEY && (ev.code == BTN_TOUCH || ev.code == BTN_TOOL_FINGER)) {
                    if (ev.value == 0) {
                        dev.slots[0].trackingId = -1;
                    } else if (dev.slots[0].trackingId < 0) {
                        dev.slots[0].trackingId = 0;
                    }
                } else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
                    bool down = false;
                    float sx = 0.0f, sy = 0.0f;
                    for (auto& s : dev.slots) {
                        if (s.trackingId >= 0) {
                            mapToScreen(dev, s.x, s.y, sx, sy);
                            down = true;
                            break;
                        }
                    }
                    std::lock_guard<std::mutex> ilk(g_inputMu);
                    g_touchDown = down;
                    if (down) g_touchPos = ImVec2(sx, sy);
                }
            }
        }
    }

    std::lock_guard<std::mutex> lk(g_devsMu);
    closeInputDevicesLocked();
    std::lock_guard<std::mutex> ilk(g_inputMu);
    g_touchDown = false;
}

bool checkEgl(const char* what) {
    EGLint err = eglGetError();
    if (err != EGL_SUCCESS) {
        LOGE("%s failed: eglGetError=0x%x", what, static_cast<unsigned>(err));
        return false;
    }
    return true;
}

bool initEGL(ANativeWindow* window, int width, int height,
             EGLDisplay& outDisplay, EGLSurface& outSurface, EGLContext& outContext) {
    outDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (outDisplay == EGL_NO_DISPLAY) {
        LOGE("eglGetDisplay failed: 0x%x", static_cast<unsigned>(eglGetError()));
        return false;
    }

    EGLint major = 0, minor = 0;
    if (eglInitialize(outDisplay, &major, &minor) != EGL_TRUE) {
        LOGE("eglInitialize failed: 0x%x", static_cast<unsigned>(eglGetError()));
        outDisplay = EGL_NO_DISPLAY;
        return false;
    }
    LOGI("EGL initialized %d.%d", major, minor);

    const EGLint cfgAttrs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_BLUE_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_RED_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 16,
        EGL_STENCIL_SIZE, 8,
        EGL_NONE
    };
    EGLConfig cfg = nullptr;
    EGLint cfgCount = 0;
    if (eglChooseConfig(outDisplay, cfgAttrs, &cfg, 1, &cfgCount) != EGL_TRUE || cfgCount <= 0 || cfg == nullptr) {
        LOGE("eglChooseConfig failed: count=%d err=0x%x", cfgCount, static_cast<unsigned>(eglGetError()));
        eglTerminate(outDisplay);
        outDisplay = EGL_NO_DISPLAY;
        return false;
    }

    EGLint fmt = 0;
    if (eglGetConfigAttrib(outDisplay, cfg, EGL_NATIVE_VISUAL_ID, &fmt) != EGL_TRUE) {
        LOGE("eglGetConfigAttrib(EGL_NATIVE_VISUAL_ID) failed: 0x%x", static_cast<unsigned>(eglGetError()));
        eglTerminate(outDisplay);
        outDisplay = EGL_NO_DISPLAY;
        return false;
    }

    ANativeWindow_setBuffersGeometry(window, width, height, fmt);

    outSurface = eglCreateWindowSurface(outDisplay, cfg, window, nullptr);
    if (outSurface == EGL_NO_SURFACE) {
        LOGE("eglCreateWindowSurface failed: 0x%x", static_cast<unsigned>(eglGetError()));
        eglTerminate(outDisplay);
        outDisplay = EGL_NO_DISPLAY;
        return false;
    }

    const EGLint ctxAttrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    outContext = eglCreateContext(outDisplay, cfg, EGL_NO_CONTEXT, ctxAttrs);
    if (outContext == EGL_NO_CONTEXT) {
        LOGE("eglCreateContext failed: 0x%x", static_cast<unsigned>(eglGetError()));
        eglDestroySurface(outDisplay, outSurface);
        outSurface = EGL_NO_SURFACE;
        eglTerminate(outDisplay);
        outDisplay = EGL_NO_DISPLAY;
        return false;
    }

    if (eglMakeCurrent(outDisplay, outSurface, outSurface, outContext) != EGL_TRUE) {
        LOGE("eglMakeCurrent failed: 0x%x", static_cast<unsigned>(eglGetError()));
        eglDestroyContext(outDisplay, outContext);
        outContext = EGL_NO_CONTEXT;
        eglDestroySurface(outDisplay, outSurface);
        outSurface = EGL_NO_SURFACE;
        eglTerminate(outDisplay);
        outDisplay = EGL_NO_DISPLAY;
        return false;
    }

    eglSwapInterval(outDisplay, 1);
    LOGI("EGL ready");
    return true;
}

void destroyEGL(EGLDisplay& display, EGLSurface& surface, EGLContext& context) {
    if (display != EGL_NO_DISPLAY) {
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (context != EGL_NO_CONTEXT) {
            eglDestroyContext(display, context);
            context = EGL_NO_CONTEXT;
        }
        if (surface != EGL_NO_SURFACE) {
            eglDestroySurface(display, surface);
            surface = EGL_NO_SURFACE;
        }
        eglTerminate(display);
        display = EGL_NO_DISPLAY;
    }
}

}  // namespace

extern "C" JNIEXPORT jint JNICALL
Java_com_android_support_V2Overlay_nativeRun(JNIEnv* env, jclass, jobject surface, jint width, jint height) {
    g_running.store(true);
    v2::ExitRequested().store(false);

    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);

    LOGI("nativeRun start %dx%d uid=%d", width, height, static_cast<int>(geteuid()));

    if (env == nullptr || surface == nullptr) {
        LOGE("nativeRun: env or surface is null");
        return 1;
    }
    if (width <= 0 || height <= 0) {
        LOGE("nativeRun: invalid size %dx%d", width, height);
        return 1;
    }

    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (window == nullptr) {
        LOGE("ANativeWindow_fromSurface returned null");
        return 1;
    }

    int winW = ANativeWindow_getWidth(window);
    int winH = ANativeWindow_getHeight(window);
    LOGI("ANativeWindow %dx%d (requested %dx%d)", winW, winH, width, height);
    if (winW <= 0 || winH <= 0) {
        winW = width;
        winH = height;
    }
    if (winW <= 0 || winH <= 0) {
        LOGE("invalid window size");
        ANativeWindow_release(window);
        return 1;
    }
    g_screenW.store(winW);
    g_screenH.store(winH);

    v2::LoadConfig();
    v2::WriteStatus();
    ext::Start();

    std::thread inThr(inputThread);

    EGLDisplay eglDisplay = EGL_NO_DISPLAY;
    EGLSurface eglSurface = EGL_NO_SURFACE;
    EGLContext eglContext = EGL_NO_CONTEXT;
    if (!initEGL(window, winW, winH, eglDisplay, eglSurface, eglContext)) {
        LOGE("initEGL failed, exiting cleanly");
        g_running.store(false);
        v2::RequestExit();
        if (inThr.joinable()) inThr.join();
        ext::Stop();
        ANativeWindow_release(window);
        return 1;
    }

    v2menu::SetupImGui();

    int frameCount = 0;
    while (g_running.load() && !v2::ExitRequested().load()) {
        EGLint curW = winW, curH = winH;
        if (eglQuerySurface(eglDisplay, eglSurface, EGL_WIDTH, &curW) == EGL_TRUE &&
            eglQuerySurface(eglDisplay, eglSurface, EGL_HEIGHT, &curH) == EGL_TRUE) {
            if (curW > 0 && curH > 0) {
                winW = curW;
                winH = curH;
                g_screenW.store(winW);
                g_screenH.store(winH);
            }
        }

        updateRotationAndSize();

        if ((++frameCount % 300) == 0) {
            v2::LoadConfig();
        }

        glViewport(0, 0, winW, winH);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(static_cast<float>(winW), static_cast<float>(winH));
        io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
        {
            std::lock_guard<std::mutex> lk(g_inputMu);
            io.MousePos = g_touchPos;
            io.MouseDown[0] = g_touchDown;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();

        ext::FrameData f = ext::Snapshot();
        f.width = winW;
        f.height = winH;
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

        if (eglSwapBuffers(eglDisplay, eglSurface) != EGL_TRUE) {
            EGLint err = eglGetError();
            LOGW("eglSwapBuffers failed: 0x%x", static_cast<unsigned>(err));
            if (err == EGL_BAD_SURFACE || err == EGL_BAD_NATIVE_WINDOW || err == EGL_CONTEXT_LOST) {
                LOGE("EGL surface/context lost, exiting");
                break;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    LOGI("nativeRun shutting down");

    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();

    destroyEGL(eglDisplay, eglSurface, eglContext);

    g_running.store(false);
    v2::RequestExit();
    if (inThr.joinable()) inThr.join();
    ext::Stop();

    ANativeWindow_release(window);

    v2::WriteStatus();
    LOGI("nativeRun exit clean");
    return 0;
}
