#pragma once

#include <cstdint>

namespace AutoRetribution {

struct Config {
    bool enabled = false;
    int hpPercentThreshold = 20;
    int cooldownMs = 500;
    bool onlyWildMonsters = true;
    bool debugLog = true;
    bool castEnabled = false;        // Safety gate: real cast invoke remains TODO
    int requiredSummonSkillId = 0;   // 0 = any summon skill
    int maxMonsterScan = 64;         // Safety cap for list iteration
    int configReloadSec = 3;         // 0 = disable periodic reload
};

// Offsets from dump.cs (user-provided: /storage/emulated/0/walib/dump.cs).
struct Offsets {
    uintptr_t battle_manager_instance = 0x0;      // BattleManager.Instance (static field)
    uintptr_t battle_manager_update = 0x330E534;  // BattleManager.Update()

    uintptr_t bm_local_player_show = 0x48;        // BattleManager.m_LocalPlayerShow
    uintptr_t bm_show_monsters = 0x78;            // BattleManager.m_ShowMonsters (List<ShowEntity>)

    uintptr_t show_entity_hp = 0x1A4;             // ShowEntity.m_Hp
    uintptr_t show_entity_hp_max = 0x1A8;         // ShowEntity.m_HpMax
    uintptr_t show_entity_dead = 0xC5;            // ShowEntity.m_bDeath
    uintptr_t show_entity_is_wild = 0x64;         // ShowEntity.IsWildMonster
    uintptr_t show_entity_camp = 0xD0;            // ShowEntity.m_EntityCampType

    uintptr_t show_player_summon_skill_id = 0x984; // ShowPlayer.m_iSummonSkillId

    // TODO: verify from target build before enabling cast.
    uintptr_t skill_comp_cast_spell_to_enemy = 0x29F0788; // SkillComponent.CastSpellToEnemy
};

void SetConfig(const Config& cfg);
Config GetConfig();
void SetOffsets(const Offsets& off);
void InstallHooks(uintptr_t il2cpp_base);
void LoadConfigFromFile(const char* path = "/data/local/tmp/v2_autoretribution.cfg");
void WriteStatus(const char* state, const char* detail = "");

}  // namespace AutoRetribution
