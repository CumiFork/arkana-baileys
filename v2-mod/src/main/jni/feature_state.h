#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

namespace v2 {

struct AutoRetriConfig {
    bool enabled = false;
    int hpPercent = 20;
    int cooldownMs = 500;
    bool onlyWild = true;
    bool castEnabled = false; // safety gate, real cast TODO
    int requiredSkillId = 0;
    int maxScan = 64;

    bool targetLord = true;
    bool targetTurtle = true;
    bool targetBlueBuff = true;
    bool targetPurpleBuff = true;
    bool targetRedBuff = true;

    // Optional monster ID overrides (0 = use type/name heuristic not available; use config IDs)
    int idLord = 0;
    int idTurtle = 0;
    int idBlueBuff = 0;
    int idPurpleBuff = 0;
    int idRedBuff = 0;
};

struct AimConfig {
    bool enabled = false;
    bool beatrixOnly = false;
    bool basicAttackOnly = true;
    float fov = 120.0f;
    float smooth = 8.0f;
    bool targetEnemyOnly = true;
};

struct EspConfig {
    bool enabled = false;
    bool showBox = true;
    bool showLine = true;
    bool showName = true;
    bool showHp = true;
    bool showDistance = false;
    bool enemyOnly = true;
    bool hideTeammates = true;
};

struct RoomInfoConfig {
    bool enabled = true;
    bool showRank = true;
    bool showTeam = true;
    bool showRoomId = false;
};

struct MenuState {
    bool open = true;
    int tab = 0;
};

struct FeatureState {
    AutoRetriConfig retri;
    AimConfig aim;
    EspConfig esp;
    RoomInfoConfig room;
    MenuState menu;
    int configReloadSec = 3;
    bool debugLog = true;
};

struct Offsets {
    uintptr_t battle_manager_instance = 0x0;
    uintptr_t battle_manager_update = 0x330E534;
    uintptr_t bm_local_player_show = 0x48;
    uintptr_t bm_show_players = 0x70;
    uintptr_t bm_show_monsters = 0x78;

    uintptr_t show_entity_hp = 0x1A4;
    uintptr_t show_entity_hp_max = 0x1A8;
    uintptr_t show_entity_dead = 0xC5;
    uintptr_t show_entity_is_wild = 0x64;
    uintptr_t show_entity_camp = 0xD0;
    uintptr_t show_entity_id = 0x18C;
    uintptr_t show_entity_guid = 0x188;
    uintptr_t show_entity_transform = 0x0; // TODO verify

    uintptr_t show_player_summon_skill_id = 0x984;
    uintptr_t show_player_rank_level = 0x92C;
    uintptr_t show_player_room_id = 0x968;
    uintptr_t show_player_zone_id = 0x960;
    uintptr_t show_player_team_name = 0x918;
    uintptr_t show_player_hero_name = 0x8F0;
    uintptr_t show_player_is_dead_field = 0x0; // TODO: use ShowEntity.m_bDeath

    uintptr_t camera_world_to_screen = 0x0; // TODO verify for current build
    uintptr_t camera_get_main = 0x0;        // TODO verify
    uintptr_t transform_get_position = 0x0;  // TODO verify
    uintptr_t component_get_transform = 0x0; // TODO verify

    // External mode: camera View-Projection Matrix
    uintptr_t camera_ptr_rva = 0x0;      // RVA from libil2cpp base to camera pointer/static
    uintptr_t camera_view_off = 0xD8;    // view/worldToCamera matrix offset from camera native obj
    uintptr_t camera_proj_off = 0xC0;    // projection matrix offset from camera native obj
    uintptr_t camera_vp_off = 0x0;       // direct VP matrix offset (if non-zero, used directly)
    uintptr_t battle_manager_instance_rva = 0x0; // alias/config
};

struct RuntimeInfo {
    std::string hookState = "init";
    std::string hookDetail = "";
    std::string retriState = "idle";
    std::string retriDetail = "";
    int localRank = -1;
    int localCamp = -1;
    std::string localTeam = "";
    uint64_t roomId = 0;
    int monsterCount = 0;
    int playerCount = 0;
    int enemyCount = 0;
    long long lastUpdateMs = 0;
    uintptr_t battleManager = 0;
    uintptr_t localPlayer = 0;
};

FeatureState& State();
std::mutex& StateMutex();
Offsets& GameOffsets();
RuntimeInfo& Info();
std::mutex& InfoMutex();

void LoadConfig(const char* path = "/data/local/tmp/v2_autoretribution.cfg");
void SaveConfig(const char* path = "/data/local/tmp/v2_autoretribution.cfg");
void WriteStatus();

}  // namespace v2
