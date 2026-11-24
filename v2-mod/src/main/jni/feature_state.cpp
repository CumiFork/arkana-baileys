#include "feature_state.h"

#include <android/log.h>
#include <chrono>
#include <fstream>
#include <sstream>

#define LOG_TAG "MEOW_V2"

namespace v2 {
namespace {

FeatureState g_state;
std::mutex g_stateMutex;
Offsets g_offsets;
RuntimeInfo g_info;
std::mutex g_infoMutex;

long long nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

bool parseBool(const std::string& v) {
    return v == "1" || v == "true" || v == "True" || v == "yes";
}

}  // namespace

FeatureState& State() { return g_state; }
std::mutex& StateMutex() { return g_stateMutex; }
Offsets& GameOffsets() { return g_offsets; }
RuntimeInfo& Info() { return g_info; }
std::mutex& InfoMutex() { return g_infoMutex; }

void LoadConfig(const char* path) {
    FeatureState s;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        s = g_state;
    }

    std::ifstream in(path ? path : "/data/local/tmp/v2_autoretribution.cfg");
    if (in.is_open()) {
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = line.substr(0, eq);
            std::string v = line.substr(eq + 1);

            if (k == "enabled") s.retri.enabled = parseBool(v);
            else if (k == "hp_percent") s.retri.hpPercent = std::atoi(v.c_str());
            else if (k == "cooldown_ms") s.retri.cooldownMs = std::atoi(v.c_str());
            else if (k == "only_wild_monsters") s.retri.onlyWild = parseBool(v);
            else if (k == "debug_log") s.debugLog = parseBool(v);
            else if (k == "cast_enabled") s.retri.castEnabled = parseBool(v);
            else if (k == "required_skill_id") s.retri.requiredSkillId = std::atoi(v.c_str());
            else if (k == "max_monster_scan") s.retri.maxScan = std::atoi(v.c_str());
            else if (k == "config_reload_sec") s.configReloadSec = std::atoi(v.c_str());

            else if (k == "retri_lord") s.retri.targetLord = parseBool(v);
            else if (k == "retri_turtle") s.retri.targetTurtle = parseBool(v);
            else if (k == "retri_blue_buff") s.retri.targetBlueBuff = parseBool(v);
            else if (k == "retri_purple_buff") s.retri.targetPurpleBuff = parseBool(v);
            else if (k == "retri_red_buff") s.retri.targetRedBuff = parseBool(v);
            else if (k == "id_lord") s.retri.idLord = std::atoi(v.c_str());
            else if (k == "id_turtle") s.retri.idTurtle = std::atoi(v.c_str());
            else if (k == "id_blue_buff") s.retri.idBlueBuff = std::atoi(v.c_str());
            else if (k == "id_purple_buff") s.retri.idPurpleBuff = std::atoi(v.c_str());
            else if (k == "id_red_buff") s.retri.idRedBuff = std::atoi(v.c_str());

            else if (k == "aim_enabled") s.aim.enabled = parseBool(v);
            else if (k == "aim_beatrix_only") s.aim.beatrixOnly = parseBool(v);
            else if (k == "aim_basic_attack") s.aim.basicAttackOnly = parseBool(v);
            else if (k == "aim_fov") s.aim.fov = std::strtof(v.c_str(), nullptr);
            else if (k == "aim_smooth") s.aim.smooth = std::strtof(v.c_str(), nullptr);
            else if (k == "aim_enemy_only") s.aim.targetEnemyOnly = parseBool(v);

            else if (k == "esp_enabled") s.esp.enabled = parseBool(v);
            else if (k == "esp_box") s.esp.showBox = parseBool(v);
            else if (k == "esp_line") s.esp.showLine = parseBool(v);
            else if (k == "esp_name") s.esp.showName = parseBool(v);
            else if (k == "esp_hp") s.esp.showHp = parseBool(v);
            else if (k == "esp_distance") s.esp.showDistance = parseBool(v);
            else if (k == "esp_enemy_only") s.esp.enemyOnly = parseBool(v);
            else if (k == "esp_hide_teammates") s.esp.hideTeammates = parseBool(v);

            else if (k == "room_enabled") s.room.enabled = parseBool(v);
            else if (k == "room_show_rank") s.room.showRank = parseBool(v);
            else if (k == "room_show_team") s.room.showTeam = parseBool(v);
            else if (k == "room_show_id") s.room.showRoomId = parseBool(v);

            else if (k == "menu_open") s.menu.open = parseBool(v);
            else if (k == "camera_ptr_rva") g_offsets.camera_ptr_rva = (uintptr_t)std::strtoull(v.c_str(), nullptr, 0);
            else if (k == "camera_view_off") g_offsets.camera_view_off = (uintptr_t)std::strtoull(v.c_str(), nullptr, 0);
            else if (k == "camera_proj_off") g_offsets.camera_proj_off = (uintptr_t)std::strtoull(v.c_str(), nullptr, 0);
            else if (k == "camera_vp_off") g_offsets.camera_vp_off = (uintptr_t)std::strtoull(v.c_str(), nullptr, 0);
            else if (k == "battle_manager_instance_rva") g_offsets.battle_manager_instance = (uintptr_t)std::strtoull(v.c_str(), nullptr, 0);
            else if (k == "battle_manager_instance") g_offsets.battle_manager_instance = (uintptr_t)std::strtoull(v.c_str(), nullptr, 0);
        }
    }

    if (s.retri.hpPercent < 1) s.retri.hpPercent = 1;
    if (s.retri.hpPercent > 100) s.retri.hpPercent = 100;
    if (s.retri.cooldownMs < 0) s.retri.cooldownMs = 0;
    if (s.retri.maxScan < 1) s.retri.maxScan = 1;
    if (s.retri.maxScan > 256) s.retri.maxScan = 256;
    if (s.configReloadSec < 0) s.configReloadSec = 0;
    if (s.aim.fov < 10.f) s.aim.fov = 10.f;
    if (s.aim.fov > 1000.f) s.aim.fov = 1000.f;
    if (s.aim.smooth < 1.f) s.aim.smooth = 1.f;

    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        g_state = s;
    }

    if (s.debugLog) {
        __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                             "Config loaded: retri=%d cast=%d lord=%d turtle=%d blue=%d purple=%d red=%d aim=%d esp=%d room=%d menu=%d",
                             s.retri.enabled, s.retri.castEnabled, s.retri.targetLord, s.retri.targetTurtle,
                             s.retri.targetBlueBuff, s.retri.targetPurpleBuff, s.retri.targetRedBuff,
                             s.aim.enabled, s.esp.enabled, s.room.enabled, s.menu.open);
    }
    WriteStatus();
}

void SaveConfig(const char* path) {
    FeatureState s;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        s = g_state;
    }
    std::ofstream out(path ? path : "/data/local/tmp/v2_autoretribution.cfg", std::ios::trunc);
    if (!out.is_open()) return;
    out << "enabled=" << (s.retri.enabled ? 1 : 0) << "\n";
    out << "hp_percent=" << s.retri.hpPercent << "\n";
    out << "cooldown_ms=" << s.retri.cooldownMs << "\n";
    out << "only_wild_monsters=" << (s.retri.onlyWild ? 1 : 0) << "\n";
    out << "debug_log=" << (s.debugLog ? 1 : 0) << "\n";
    out << "cast_enabled=" << (s.retri.castEnabled ? 1 : 0) << "\n";
    out << "required_skill_id=" << s.retri.requiredSkillId << "\n";
    out << "max_monster_scan=" << s.retri.maxScan << "\n";
    out << "config_reload_sec=" << s.configReloadSec << "\n";
    out << "retri_lord=" << (s.retri.targetLord ? 1 : 0) << "\n";
    out << "retri_turtle=" << (s.retri.targetTurtle ? 1 : 0) << "\n";
    out << "retri_blue_buff=" << (s.retri.targetBlueBuff ? 1 : 0) << "\n";
    out << "retri_purple_buff=" << (s.retri.targetPurpleBuff ? 1 : 0) << "\n";
    out << "retri_red_buff=" << (s.retri.targetRedBuff ? 1 : 0) << "\n";
    out << "id_lord=" << s.retri.idLord << "\n";
    out << "id_turtle=" << s.retri.idTurtle << "\n";
    out << "id_blue_buff=" << s.retri.idBlueBuff << "\n";
    out << "id_purple_buff=" << s.retri.idPurpleBuff << "\n";
    out << "id_red_buff=" << s.retri.idRedBuff << "\n";
    out << "aim_enabled=" << (s.aim.enabled ? 1 : 0) << "\n";
    out << "aim_beatrix_only=" << (s.aim.beatrixOnly ? 1 : 0) << "\n";
    out << "aim_basic_attack=" << (s.aim.basicAttackOnly ? 1 : 0) << "\n";
    out << "aim_fov=" << s.aim.fov << "\n";
    out << "aim_smooth=" << s.aim.smooth << "\n";
    out << "aim_enemy_only=" << (s.aim.targetEnemyOnly ? 1 : 0) << "\n";
    out << "esp_enabled=" << (s.esp.enabled ? 1 : 0) << "\n";
    out << "esp_box=" << (s.esp.showBox ? 1 : 0) << "\n";
    out << "esp_line=" << (s.esp.showLine ? 1 : 0) << "\n";
    out << "esp_name=" << (s.esp.showName ? 1 : 0) << "\n";
    out << "esp_hp=" << (s.esp.showHp ? 1 : 0) << "\n";
    out << "esp_distance=" << (s.esp.showDistance ? 1 : 0) << "\n";
    out << "esp_enemy_only=" << (s.esp.enemyOnly ? 1 : 0) << "\n";
    out << "esp_hide_teammates=" << (s.esp.hideTeammates ? 1 : 0) << "\n";
    out << "room_enabled=" << (s.room.enabled ? 1 : 0) << "\n";
    out << "room_show_rank=" << (s.room.showRank ? 1 : 0) << "\n";
    out << "room_show_team=" << (s.room.showTeam ? 1 : 0) << "\n";
    out << "room_show_id=" << (s.room.showRoomId ? 1 : 0) << "\n";
    out << "menu_open=" << (s.menu.open ? 1 : 0) << "\n";
    out << "battle_manager_instance_rva=" << g_offsets.battle_manager_instance << "\n";
    out << "camera_ptr_rva=" << g_offsets.camera_ptr_rva << "\n";
    out << "camera_view_off=" << g_offsets.camera_view_off << "\n";
    out << "camera_proj_off=" << g_offsets.camera_proj_off << "\n";
    out << "camera_vp_off=" << g_offsets.camera_vp_off << "\n";
}

void WriteStatus() {
    FeatureState s;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        s = g_state;
    }
    RuntimeInfo i;
    {
        std::lock_guard<std::mutex> lock(g_infoMutex);
        i = g_info;
    }
    std::ofstream out("/data/local/tmp/v2_autoretribution.status", std::ios::trunc);
    if (!out.is_open()) return;
    out << "state=" << i.hookState << "\n";
    out << "detail=" << i.hookDetail << "\n";
    out << "retri_state=" << i.retriState << "\n";
    out << "retri_detail=" << i.retriDetail << "\n";
    out << "ts_ms=" << nowMs() << "\n";
    out << "menu_open=" << (s.menu.open ? 1 : 0) << "\n";
    out << "battle_manager_instance_rva=" << g_offsets.battle_manager_instance << "\n";
    out << "camera_ptr_rva=" << g_offsets.camera_ptr_rva << "\n";
    out << "camera_view_off=" << g_offsets.camera_view_off << "\n";
    out << "camera_proj_off=" << g_offsets.camera_proj_off << "\n";
    out << "camera_vp_off=" << g_offsets.camera_vp_off << "\n";
    out << "enabled=" << (s.retri.enabled ? 1 : 0) << "\n";
    out << "cast_enabled=" << (s.retri.castEnabled ? 1 : 0) << "\n";
    out << "aim_enabled=" << (s.aim.enabled ? 1 : 0) << "\n";
    out << "esp_enabled=" << (s.esp.enabled ? 1 : 0) << "\n";
    out << "room_enabled=" << (s.room.enabled ? 1 : 0) << "\n";
    out << "local_rank=" << i.localRank << "\n";
    out << "local_camp=" << i.localCamp << "\n";
    out << "room_id=" << i.roomId << "\n";
    out << "monster_count=" << i.monsterCount << "\n";
    out << "player_count=" << i.playerCount << "\n";
    out << "enemy_count=" << i.enemyCount << "\n";
}

}  // namespace v2
