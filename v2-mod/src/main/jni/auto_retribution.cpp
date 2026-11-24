#include "auto_retribution.h"

#include <android/log.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>

#include "include/Hook/Dobby/dobby.h"

#define LOG_TAG "MEOW_V2"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace AutoRetribution {
namespace {

std::mutex g_mutex;
Config g_config;
Offsets g_offsets;
uintptr_t g_il2cpp_base = 0;
std::atomic<bool> g_installed{false};
std::atomic<long long> g_lastCastMs{0};
std::atomic<long long> g_lastConfigCheckMs{0};
std::string g_configPath = "/data/local/tmp/v2_autoretribution.cfg";
constexpr const char* kStatusPath = "/data/local/tmp/v2_autoretribution.status";

using UpdateFn = void (*)(void* instance);
UpdateFn orig_update = nullptr;

long long nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

template <typename T>
T readPtr(void* obj, uintptr_t off) {
    if (!obj) return T{};
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(obj) + off);
}

template <typename T>
T readField(void* obj, uintptr_t off) {
    if (!obj) return T{};
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(obj) + off);
}

int getListSize(void* list) {
    if (!list) return 0;
    return readField<int>(list, 0x18);
}

void* getListItem(void* list, int index) {
    if (!list || index < 0) return nullptr;
    void* items = readPtr<void*>(list, 0x10);
    if (!items) return nullptr;
    return readPtr<void*>(items, 0x20 + static_cast<uintptr_t>(index) * sizeof(void*));
}

void writeStatusLocked(const char* state, const char* detail) {
    std::ofstream out(kStatusPath, std::ios::trunc);
    if (!out.is_open()) return;
    out << "state=" << (state ? state : "") << "\n";
    out << "detail=" << (detail ? detail : "") << "\n";
    out << "ts_ms=" << nowMs() << "\n";
}

bool tryCastRetribution(void* localPlayer, void* targetMonster, int summonSkillId) {
    // TODO: resolve LogicFighter/AIAgent SkillComponent and call CastSpellToEnemy.
    // Dump RVA reference: SkillComponent.CastSpellToEnemy = 0x29F0788.
    // Signature: SPELL_CAST_RESULT CastSpellToEnemy(LogicFighter*, int skillId, bool press, bool inFirstDragRange, bool firstTarget, bool useNoTarget)
    // Safety: cast is gated by cfg.castEnabled and remains unimplemented until verified on target build.
    (void)localPlayer;
    (void)targetMonster;
    (void)summonSkillId;
    return false;
}

void doLoadConfig(const char* path) {
    Config cfg;
    cfg.enabled = true;
    cfg.hpPercentThreshold = 20;
    cfg.cooldownMs = 500;
    cfg.onlyWildMonsters = true;
    cfg.debugLog = true;
    cfg.castEnabled = false;
    cfg.requiredSummonSkillId = 0;
    cfg.maxMonsterScan = 64;
    cfg.configReloadSec = 3;

    Offsets off;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        off = g_offsets;
    }

    const char* cfgPath = (path && *path) ? path : g_configPath.c_str();
    std::ifstream in(cfgPath);
    if (in.is_open()) {
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);
            if (key == "enabled") cfg.enabled = (val == "1" || val == "true");
            else if (key == "hp_percent") cfg.hpPercentThreshold = std::atoi(val.c_str());
            else if (key == "cooldown_ms") cfg.cooldownMs = std::atoi(val.c_str());
            else if (key == "only_wild_monsters") cfg.onlyWildMonsters = (val == "1" || val == "true");
            else if (key == "debug_log") cfg.debugLog = (val == "1" || val == "true");
            else if (key == "cast_enabled") cfg.castEnabled = (val == "1" || val == "true");
            else if (key == "required_skill_id") cfg.requiredSummonSkillId = std::atoi(val.c_str());
            else if (key == "max_monster_scan") cfg.maxMonsterScan = std::atoi(val.c_str());
            else if (key == "config_reload_sec") cfg.configReloadSec = std::atoi(val.c_str());
        }
    }

    if (cfg.hpPercentThreshold < 1) cfg.hpPercentThreshold = 1;
    if (cfg.hpPercentThreshold > 100) cfg.hpPercentThreshold = 100;
    if (cfg.cooldownMs < 0) cfg.cooldownMs = 0;
    if (cfg.maxMonsterScan < 1) cfg.maxMonsterScan = 1;
    if (cfg.maxMonsterScan > 256) cfg.maxMonsterScan = 256;
    if (cfg.configReloadSec < 0) cfg.configReloadSec = 0;

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_config = cfg;
        g_offsets = off;
        if (path && *path) g_configPath = path;
    }

    if (cfg.debugLog) {
        LOGI("Config: enabled=%d hp=%d cd=%d wild=%d cast=%d reqSkill=%d scan=%d reload=%ds",
             cfg.enabled, cfg.hpPercentThreshold, cfg.cooldownMs, cfg.onlyWildMonsters,
             cfg.castEnabled, cfg.requiredSummonSkillId, cfg.maxMonsterScan, cfg.configReloadSec);
    }
    writeStatusLocked("config_loaded", "ok");
}

void hooked_battle_update(void* instance) {
    if (orig_update) orig_update(instance);

    Config cfg;
    Offsets off;
    uintptr_t base;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        cfg = g_config;
        off = g_offsets;
        base = g_il2cpp_base;
    }

    long long ms = nowMs();
    if (cfg.configReloadSec > 0) {
        long long lastCheck = g_lastConfigCheckMs.load();
        if ((ms - lastCheck) >= static_cast<long long>(cfg.configReloadSec) * 1000) {
            g_lastConfigCheckMs.store(ms);
            doLoadConfig(g_configPath.c_str());
            std::lock_guard<std::mutex> lock(g_mutex);
            cfg = g_config;
            off = g_offsets;
        }
    }

    if (!cfg.enabled || instance == nullptr || base == 0) return;

    long long last = g_lastCastMs.load();
    if (cfg.cooldownMs > 0 && (ms - last) < cfg.cooldownMs) return;

    void* localPlayer = readPtr<void*>(instance, off.bm_local_player_show);
    if (!localPlayer) return;

    int summonSkillId = readField<int>(localPlayer, off.show_player_summon_skill_id);
    if (summonSkillId == 0) return;
    if (cfg.requiredSummonSkillId != 0 && summonSkillId != cfg.requiredSummonSkillId) return;

    void* monsters = readPtr<void*>(instance, off.bm_show_monsters);
    int count = getListSize(monsters);
    if (count < 0) return;
    if (cfg.maxMonsterScan > 0 && count > cfg.maxMonsterScan) count = cfg.maxMonsterScan;

    for (int i = 0; i < count; ++i) {
        void* monster = getListItem(monsters, i);
        if (!monster) continue;

        bool isDead = readField<bool>(monster, off.show_entity_dead);
        if (isDead) continue;

        if (cfg.onlyWildMonsters) {
            bool isWild = readField<bool>(monster, off.show_entity_is_wild);
            if (!isWild) continue;
        }

        int hp = readField<int>(monster, off.show_entity_hp);
        int hpMax = readField<int>(monster, off.show_entity_hp_max);
        if (hpMax <= 0 || hp <= 0) continue;

        int hpPercent = static_cast<int>((static_cast<long long>(hp) * 100) / hpMax);
        if (hpPercent <= cfg.hpPercentThreshold) {
            if (cfg.debugLog) {
                LOGI("Retribution target: skill=%d hp=%d/%d (%d%%) cast_enabled=%d",
                     summonSkillId, hp, hpMax, hpPercent, cfg.castEnabled ? 1 : 0);
            }
            writeStatusLocked("target_detected", "monster_in_threshold");
            if (cfg.castEnabled && tryCastRetribution(localPlayer, monster, summonSkillId)) {
                g_lastCastMs.store(ms);
                writeStatusLocked("cast_attempted", "success");
            } else {
                g_lastCastMs.store(ms);
                writeStatusLocked(cfg.castEnabled ? "cast_attempted" : "detected_only",
                                  cfg.castEnabled ? "not_implemented" : "cast_disabled");
            }
            break;
        }
    }
}

}  // namespace

void SetConfig(const Config& cfg) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_config = cfg;
}

Config GetConfig() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_config;
}

void SetOffsets(const Offsets& off) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_offsets = off;
}

void WriteStatus(const char* state, const char* detail) {
    std::ofstream out(kStatusPath, std::ios::trunc);
    if (!out.is_open()) return;
    out << "state=" << (state ? state : "") << "\n";
    out << "detail=" << (detail ? detail : "") << "\n";
    out << "ts_ms=" << nowMs() << "\n";
}

void InstallHooks(uintptr_t il2cpp_base) {
    if (g_installed.exchange(true)) return;
    if (il2cpp_base == 0) {
        LOGW("InstallHooks: il2cpp_base is 0");
        WriteStatus("hook_failed", "il2cpp_base_zero");
        g_installed.store(false);
        return;
    }

    Offsets off;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        off = g_offsets;
        g_il2cpp_base = il2cpp_base;
    }

    if (off.battle_manager_update == 0) {
        LOGW("BattleManager.Update offset is 0");
        WriteStatus("hook_failed", "update_offset_zero");
        g_installed.store(false);
        return;
    }

    uintptr_t target = il2cpp_base + off.battle_manager_update;
    int rc = DobbyHook(reinterpret_cast<void*>(target), reinterpret_cast<void*>(hooked_battle_update),
                       reinterpret_cast<void**>(&orig_update));
    if (rc == 0) {
        LOGI("BattleManager.Update hooked at %p", reinterpret_cast<void*>(target));
        WriteStatus("hook_installed", "battle_manager_update");
    } else {
        LOGW("DobbyHook failed rc=%d target=%p", rc, reinterpret_cast<void*>(target));
        WriteStatus("hook_failed", "dobby_hook_error");
        g_installed.store(false);
    }
}

void LoadConfigFromFile(const char* path) {
    doLoadConfig(path);
}

}  // namespace AutoRetribution
