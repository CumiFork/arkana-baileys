#include "auto_retribution.h"

#include <android/log.h>
#include <atomic>
#include <chrono>
#include <mutex>

#include "feature_state.h"
#include "include/Hook/Dobby/dobby.h"

#define LOG_TAG "MEOW_V2"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace AutoRetribution {
namespace {

std::atomic<bool> g_installed{false};
std::atomic<long long> g_lastCastMs{0};
std::atomic<long long> g_lastConfigCheckMs{0};
std::atomic<long long> g_lastStatusMs{0};
uintptr_t g_il2cpp_base = 0;

using UpdateFn = void (*)(void* instance);
UpdateFn orig_update = nullptr;

long long nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

template <typename T>
T readPtr(void* obj, uintptr_t off) {
    if (!obj || !off) return T{};
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(obj) + off);
}

template <typename T>
T readVal(void* obj, uintptr_t off) {
    if (!obj) return T{};
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(obj) + off);
}

int getListSize(void* list) {
    if (!list) return 0;
    return readVal<int>(list, 0x18);
}

void* getListItem(void* list, int index) {
    if (!list || index < 0) return nullptr;
    void* items = readPtr<void*>(list, 0x10);
    if (!items) return nullptr;
    return readPtr<void*>(items, 0x20 + static_cast<uintptr_t>(index) * sizeof(void*));
}

const char* monsterCategory(int monsterId, const v2::AutoRetriConfig& cfg) {
    if (cfg.idLord != 0 && monsterId == cfg.idLord) return "lord";
    if (cfg.idTurtle != 0 && monsterId == cfg.idTurtle) return "turtle";
    if (cfg.idBlueBuff != 0 && monsterId == cfg.idBlueBuff) return "blue_buff";
    if (cfg.idPurpleBuff != 0 && monsterId == cfg.idPurpleBuff) return "purple_buff";
    if (cfg.idRedBuff != 0 && monsterId == cfg.idRedBuff) return "red_buff";
    return nullptr;
}

bool categoryEnabled(const char* cat, const v2::AutoRetriConfig& cfg) {
    if (!cat) return false;
    if (strcmp(cat, "lord") == 0) return cfg.targetLord;
    if (strcmp(cat, "turtle") == 0) return cfg.targetTurtle;
    if (strcmp(cat, "blue_buff") == 0) return cfg.targetBlueBuff;
    if (strcmp(cat, "purple_buff") == 0) return cfg.targetPurpleBuff;
    if (strcmp(cat, "red_buff") == 0) return cfg.targetRedBuff;
    return false;
}

bool hasAnyConfiguredId(const v2::AutoRetriConfig& cfg) {
    return cfg.idLord || cfg.idTurtle || cfg.idBlueBuff || cfg.idPurpleBuff || cfg.idRedBuff;
}

bool tryCastRetribution(void* localPlayer, void* targetMonster, int summonSkillId) {
    // TODO: Resolve SkillComponent and invoke CastSpellToEnemy (RVA 0x29F0788 in dump).
    // Signature reference: SPELL_CAST_RESULT CastSpellToEnemy(LogicFighter*, int skillId, bool, bool, bool, bool)
    // Kept gated by castEnabled until verified on target build.
    (void)localPlayer;
    (void)targetMonster;
    (void)summonSkillId;
    return false;
}

void hooked_battle_update(void* instance) {
    if (orig_update) orig_update(instance);

    v2::FeatureState s;
    {
        std::lock_guard<std::mutex> lock(v2::StateMutex());
        s = v2::State();
    }
    v2::Offsets off = v2::GameOffsets();

    long long ms = nowMs();
    if (s.configReloadSec > 0) {
        long long lastCheck = g_lastConfigCheckMs.load();
        if ((ms - lastCheck) >= static_cast<long long>(s.configReloadSec) * 1000) {
            g_lastConfigCheckMs.store(ms);
            v2::LoadConfig();
            std::lock_guard<std::mutex> lock(v2::StateMutex());
            s = v2::State();
        }
    }

    if (!s.retri.enabled || instance == nullptr || g_il2cpp_base == 0) return;

    long long last = g_lastCastMs.load();
    if (s.retri.cooldownMs > 0 && (ms - last) < s.retri.cooldownMs) return;

    void* localPlayer = readPtr<void*>(instance, off.bm_local_player_show);

    // Room/player info (best-effort, uses dump-derived offsets)
    if (localPlayer) {
        int localCamp = readVal<int>(localPlayer, off.show_entity_camp);
        int localRank = readVal<int>(localPlayer, off.show_player_rank_level);
        uint64_t roomId = readVal<uint64_t>(localPlayer, off.show_player_room_id);
        void* players = readPtr<void*>(instance, off.bm_show_players);
        int pcount = getListSize(players);
        int enemies = 0;
        int scanP = pcount;
        if (scanP > 20) scanP = 20;
        for (int i = 0; i < scanP; ++i) {
            void* p = getListItem(players, i);
            if (!p || p == localPlayer) continue;
            bool dead = readVal<bool>(p, off.show_entity_dead);
            if (dead) continue;
            int camp = readVal<int>(p, off.show_entity_camp);
            if (localCamp != 0 && camp != 0 && camp != localCamp) enemies++;
        }
        std::lock_guard<std::mutex> lk(v2::InfoMutex());
        v2::Info().localCamp = localCamp;
        v2::Info().localRank = localRank;
        v2::Info().roomId = roomId;
        v2::Info().playerCount = pcount;
        v2::Info().enemyCount = enemies;
    }

    long long lastStatus = g_lastStatusMs.load();
    if ((ms - lastStatus) >= 2000) {
        g_lastStatusMs.store(ms);
        v2::WriteStatus();
    }

    if (!s.retri.enabled) return;
    if (!localPlayer) return;

    int summonSkillId = readVal<int>(localPlayer, off.show_player_summon_skill_id);
    if (summonSkillId == 0) return;
    if (s.retri.requiredSkillId != 0 && summonSkillId != s.retri.requiredSkillId) return;

    void* monsters = readPtr<void*>(instance, off.bm_show_monsters);
    int count = getListSize(monsters);
    if (count < 0) return;
    int scan = count;
    if (s.retri.maxScan > 0 && scan > s.retri.maxScan) scan = s.retri.maxScan;

    {
        std::lock_guard<std::mutex> lk(v2::InfoMutex());
        v2::Info().monsterCount = count;
        v2::Info().lastUpdateMs = ms;
    }

    bool anyConfigured = hasAnyConfiguredId(s.retri);

    for (int i = 0; i < scan; ++i) {
        void* monster = getListItem(monsters, i);
        if (!monster) continue;

        bool isDead = readVal<bool>(monster, off.show_entity_dead);
        if (isDead) continue;

        if (s.retri.onlyWild) {
            bool isWild = readVal<bool>(monster, off.show_entity_is_wild);
            if (!isWild) continue;
        }

        int monsterId = readVal<int>(monster, off.show_entity_id);
        const char* cat = monsterCategory(monsterId, s.retri);

        // If user configured specific IDs, only match enabled categories.
        // If no IDs configured, allow any wild monster (legacy behavior) so feature is testable.
        if (anyConfigured) {
            if (!cat || !categoryEnabled(cat, s.retri)) continue;
        }

        int hp = readVal<int>(monster, off.show_entity_hp);
        int hpMax = readVal<int>(monster, off.show_entity_hp_max);
        if (hpMax <= 0 || hp <= 0) continue;

        int hpPercent = static_cast<int>((static_cast<long long>(hp) * 100) / hpMax);
        if (hpPercent <= s.retri.hpPercent) {
            if (s.debugLog) {
                LOGI("Retri target: id=%d cat=%s hp=%d/%d (%d%%) skill=%d cast=%d",
                     monsterId, cat ? cat : "wild", hp, hpMax, hpPercent, summonSkillId, s.retri.castEnabled ? 1 : 0);
            }
            {
                std::lock_guard<std::mutex> lk(v2::InfoMutex());
                v2::Info().retriState = "target_detected";
                v2::Info().retriDetail = cat ? cat : "wild";
            }
            v2::WriteStatus();
            if (s.retri.castEnabled && tryCastRetribution(localPlayer, monster, summonSkillId)) {
                g_lastCastMs.store(ms);
                std::lock_guard<std::mutex> lk(v2::InfoMutex());
                v2::Info().retriState = "cast_attempted";
                v2::Info().retriDetail = "success";
            } else {
                g_lastCastMs.store(ms);
                std::lock_guard<std::mutex> lk(v2::InfoMutex());
                v2::Info().retriState = s.retri.castEnabled ? "cast_attempted" : "detected_only";
                v2::Info().retriDetail = s.retri.castEnabled ? "not_implemented" : "cast_disabled";
            }
            v2::WriteStatus();
            break;
        }
    }
}

}  // namespace

void SetConfig(const Config& cfg) {
    std::lock_guard<std::mutex> lock(v2::StateMutex());
    auto& s = v2::State();
    s.retri.enabled = cfg.enabled;
    s.retri.hpPercent = cfg.hpPercentThreshold;
    s.retri.cooldownMs = cfg.cooldownMs;
    s.retri.onlyWild = cfg.onlyWildMonsters;
    s.debugLog = cfg.debugLog;
    s.retri.castEnabled = cfg.castEnabled;
    s.retri.requiredSkillId = cfg.requiredSummonSkillId;
    s.retri.maxScan = cfg.maxMonsterScan;
    s.configReloadSec = cfg.configReloadSec;
}

Config GetConfig() {
    Config cfg;
    std::lock_guard<std::mutex> lock(v2::StateMutex());
    auto& s = v2::State();
    cfg.enabled = s.retri.enabled;
    cfg.hpPercentThreshold = s.retri.hpPercent;
    cfg.cooldownMs = s.retri.cooldownMs;
    cfg.onlyWildMonsters = s.retri.onlyWild;
    cfg.debugLog = s.debugLog;
    cfg.castEnabled = s.retri.castEnabled;
    cfg.requiredSummonSkillId = s.retri.requiredSkillId;
    cfg.maxMonsterScan = s.retri.maxScan;
    cfg.configReloadSec = s.configReloadSec;
    return cfg;
}

void SetOffsets(const Offsets& off) {
    v2::GameOffsets().battle_manager_instance = off.battle_manager_instance;
    v2::GameOffsets().battle_manager_update = off.battle_manager_update;
    v2::GameOffsets().bm_local_player_show = off.bm_local_player_show;
    v2::GameOffsets().bm_show_monsters = off.bm_show_monsters;
    v2::GameOffsets().show_entity_hp = off.show_entity_hp;
    v2::GameOffsets().show_entity_hp_max = off.show_entity_hp_max;
    v2::GameOffsets().show_entity_dead = off.show_entity_dead;
    v2::GameOffsets().show_entity_is_wild = off.show_entity_is_wild;
    v2::GameOffsets().show_entity_camp = off.show_entity_camp;
    v2::GameOffsets().show_player_summon_skill_id = off.show_player_summon_skill_id;
}

void WriteStatus(const char* state, const char* detail) {
    std::lock_guard<std::mutex> lk(v2::InfoMutex());
    v2::Info().hookState = state ? state : "";
    v2::Info().hookDetail = detail ? detail : "";
    v2::WriteStatus();
}

void InstallHooks(uintptr_t il2cpp_base) {
    if (g_installed.exchange(true)) return;
    g_il2cpp_base = il2cpp_base;
    if (il2cpp_base == 0) {
        WriteStatus("hook_failed", "il2cpp_base_zero");
        g_installed.store(false);
        return;
    }

    uintptr_t target = il2cpp_base + v2::GameOffsets().battle_manager_update;
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
    v2::LoadConfig(path);
}

}  // namespace AutoRetribution
