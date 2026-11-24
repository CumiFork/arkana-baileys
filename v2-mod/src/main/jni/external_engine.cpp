#include "external_engine.h"

#include <android/log.h>
#include <dirent.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <mutex>
#include <thread>

#include "mem_reader.h"

#define LOG_TAG "MEOW_V2EXT"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace ext {
namespace {

std::mutex g_mu;
FrameData g_frame;
std::thread g_thread;
bool g_running = false;

long long nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

pid_t findPid(const char* procName) {
    DIR* dir = opendir("/proc");
    if (!dir) return -1;
    struct dirent* ent;
    pid_t found = -1;
    while ((ent = readdir(dir)) != nullptr) {
        if (ent->d_type != DT_DIR) continue;
        char* endp = nullptr;
        long pid = strtol(ent->d_name, &endp, 10);
        if (!endp || *endp) continue;
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "/proc/%ld/cmdline", pid);
        std::ifstream in(cmd, std::ios::binary);
        std::string name;
        std::getline(in, name, '\0');
        if (name == procName) { found = (pid_t)pid; break; }
    }
    closedir(dir);
    return found;
}

void matMul(const float a[16], const float b[16], float out[16]) {
    // column-major: out = a*b
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            out[c*4+r] = a[0*4+r]*b[c*4+0] + a[1*4+r]*b[c*4+1] + a[2*4+r]*b[c*4+2] + a[3*4+r]*b[c*4+3];
        }
    }
}

bool readMatrix(uintptr_t addr, float out[16]) {
    return readMem(addr, out, sizeof(float)*16);
}

uintptr_t readPtr(uintptr_t addr) { return read<uintptr_t>(addr, 0); }
int readInt(uintptr_t addr) { return read<int>(addr, 0); }
uint32_t readU32(uintptr_t addr) { return read<uint32_t>(addr, 0); }
uint64_t readU64(uintptr_t addr) { return read<uint64_t>(addr, 0); }
bool readBool(uintptr_t addr) { return read<uint8_t>(addr, 0) != 0; }

uintptr_t resolveBattleManager(uintptr_t il2cppBase) {
    auto& off = v2::GameOffsets();
    // Strategy 1: config/known RVA as pointer to instance (static field)
    if (off.battle_manager_instance != 0) {
        uintptr_t p = readPtr(il2cppBase + off.battle_manager_instance);
        if (p) return p;
        // maybe it's an absolute pointer
        p = readPtr(off.battle_manager_instance);
        if (p) return p;
    }
    return 0;
}

uintptr_t findCamera(uintptr_t il2cppBase, uintptr_t battleManager) {
    auto& off = v2::GameOffsets();
    if (off.camera_ptr_rva != 0) {
        uintptr_t p = readPtr(il2cppBase + off.camera_ptr_rva);
        if (p) return p;
        p = readPtr(off.camera_ptr_rva);
        if (p) return p;
    }
    if (off.camera_get_main != 0) {
        uintptr_t p = readPtr(il2cppBase + off.camera_get_main);
        if (p) return p;
    }
    (void)battleManager;
    return 0;
}

bool readViewProj(uintptr_t camera, FrameData& f) {
    auto& off = v2::GameOffsets();
    f.hasMatrix = false;
    if (!camera) return false;

    auto tryPair = [&](uintptr_t viewOff, uintptr_t projOff) -> bool {
        float v[16], p[16];
        if (!readMatrix(camera + viewOff, v) || !readMatrix(camera + projOff, p)) return false;
        float p00 = p[0], p11 = p[5];
        if (std::fabs(p00) < 0.01f && std::fabs(p11) < 0.01f) return false;
        if (std::fabs(p[15]) > 0.01f && std::fabs(p[15]-1.0f) > 0.01f && std::fabs(p[14]) < 0.01f) return false;
        memcpy(f.view, v, sizeof(v));
        memcpy(f.proj, p, sizeof(p));
        matMul(p, v, f.vp);
        f.hasMatrix = true;
        f.camera = camera;
        return true;
    };

    if (off.camera_vp_off != 0) {
        float vp[16];
        if (readMatrix(camera + off.camera_vp_off, vp)) {
            memcpy(f.vp, vp, sizeof(vp));
            f.hasMatrix = true;
            f.camera = camera;
            return true;
        }
    }

    if (off.camera_view_off != 0 && off.camera_proj_off != 0) {
        if (tryPair(off.camera_view_off, off.camera_proj_off)) return true;
    }

    struct Pair { uintptr_t view; uintptr_t proj; };
    Pair candidates[] = {
        {0xD8, 0xC0}, {0xE0, 0xD0}, {0x124, 0x84}, {0xFC, 0xBC}, {0x98, 0xD8},
        {0x1C4, 0x204}, {0x2C, 0x6C},
    };
    for (auto& c : candidates) {
        if (tryPair(c.view, c.proj)) return true;
    }
    return false;
}

void readEntities(FrameData& f) {
    auto& off = v2::GameOffsets();
    f.playerCount = 0;
    f.enemyCount = 0;
    f.localRank = -1;
    f.localCamp = 0;
    f.roomId = 0;
    if (!f.battleManager) return;

    uintptr_t local = readPtr(f.battleManager + off.bm_local_player_show);
    f.localPlayer = local;
    if (local) {
        f.localCamp = readInt(local + off.show_entity_camp);
        f.localRank = readInt(local + off.show_player_rank_level);
        f.roomId = readU64(local + off.show_player_room_id);
    }

    uintptr_t playersList = readPtr(f.battleManager + off.bm_show_players);
    if (playersList) {
        int count = readInt(playersList + 0x18);
        if (count < 0) count = 0;
        if (count > 32) count = 32;
        uintptr_t items = readPtr(playersList + 0x10);
        for (int i = 0; i < count; ++i) {
            uintptr_t p = readPtr(items + 0x20 + (uintptr_t)i * sizeof(void*));
            if (!p) continue;
            if (readBool(p + off.show_entity_dead)) continue;
            f.playerCount++;
            int camp = readInt(p + off.show_entity_camp);
            if (f.localCamp != 0 && camp != 0 && camp != f.localCamp) f.enemyCount++;
        }
    }
}

void loop() {
    LOGI("external engine thread started");
    while (true) {
        {
            std::lock_guard<std::mutex> lk(g_mu);
            if (!g_running) break;
        }

        FrameData next = {};
        next.status = "searching";
        next.detail = "waiting for com.mobile.legends:UnityKillsMe";

        pid_t pid = findPid("com.mobile.legends:UnityKillsMe");
        if (pid <= 0) {
            closeReader();
            std::lock_guard<std::mutex> lk(g_mu);
            g_frame = next;
            g_frame.status = "no_process";
            g_frame.detail = "com.mobile.legends:UnityKillsMe not found";
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            continue;
        }

        if (targetPid() != pid) {
            if (!initReader(pid)) {
                std::lock_guard<std::mutex> lk(g_mu);
                g_frame = next;
                g_frame.status = "attach_failed";
                g_frame.detail = "open /proc/pid/mem failed; need root";
                std::this_thread::sleep_for(std::chrono::milliseconds(1500));
                continue;
            }
            LOGI("attached pid=%d", (int)pid);
        }

        uintptr_t il2cpp = getModuleBase("libil2cpp.so");
        if (!il2cpp) {
            next.status = "no_il2cpp";
            next.detail = "libil2cpp.so not mapped in target";
            std::lock_guard<std::mutex> lk(g_mu);
            g_frame = next;
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            continue;
        }

        next.status = "attached";
        next.detail = "pid attached, il2cpp found";
        next.battleManager = resolveBattleManager(il2cpp);
        if (next.battleManager) {
            next.status = "bm_found";
            next.detail = "BattleManager resolved";
        } else {
            next.detail = "BattleManager.Instance not resolved; set battle_manager_instance RVA in config";
        }

        readEntities(next);

        uintptr_t cam = findCamera(il2cpp, next.battleManager);
        if (cam) readViewProj(cam, next);
        if (!next.hasMatrix) {
            // try to read camera from Unity main camera? no - need il2cpp call. Keep status.
        }

        {
            std::lock_guard<std::mutex> lk(g_mu);
            g_frame = next;
        }

        // update runtime info
        static long long lastStatusMs = 0;
        {
            std::lock_guard<std::mutex> lk(v2::InfoMutex());
            auto& i = v2::Info();
            i.hookState = next.status;
            i.hookDetail = next.detail;
            i.battleManager = next.battleManager;
            i.localPlayer = next.localPlayer;
            i.localCamp = next.localCamp;
            i.localRank = next.localRank;
            i.roomId = next.roomId;
            i.playerCount = next.playerCount;
            i.enemyCount = next.enemyCount;
            i.lastUpdateMs = nowMs();
        }
        long long now = nowMs();
        if (now - lastStatusMs > 2000) {
            lastStatusMs = now;
            v2::LoadConfig();
            v2::WriteStatus();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    closeReader();
    LOGI("external engine thread stopped");
}

}  // namespace

bool Start() {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_running) return true;
    g_running = true;
    g_thread = std::thread(loop);
    return true;
}

void Stop() {
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_running = false;
    }
    if (g_thread.joinable()) g_thread.join();
}

FrameData Snapshot() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_frame;
}

bool WorldToScreen(const FrameData& f, const game::Vec3& world, game::Vec3& out) {
    if (!f.hasMatrix) return false;
    const float* m = f.vp;
    float x = world.x, y = world.y, z = world.z;
    float cx = m[0]*x + m[4]*y + m[8]*z + m[12];
    float cy = m[1]*x + m[5]*y + m[9]*z + m[13];
    float cw = m[3]*x + m[7]*y + m[11]*z + m[15];
    if (cw < 0.001f) return false;
    float nx = cx / cw;
    float ny = cy / cw;
    float w = (float)(f.width > 0 ? f.width : 1920);
    float h = (float)(f.height > 0 ? f.height : 1080);
    out.x = (nx + 1.0f) * 0.5f * w;
    out.y = (1.0f - ny) * 0.5f * h;
    out.z = cw;
    return true;
}

}  // namespace ext
