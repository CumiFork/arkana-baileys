#include "menu.h"

#include <android/log.h>
#include <GLES3/gl3.h>
#include <cstdio>
#include <cstring>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "feature_state.h"
#include "game_api.h"

#define LOG_TAG "MEOW_V2"

namespace v2menu {
namespace {

bool g_setup = false;

void HelpMarker(const char* desc) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

}  // namespace

void SetupImGui() {
    if (g_setup) return;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.WindowPadding = ImVec2(10, 10);
    style.FramePadding = ImVec2(6, 4);
    style.ItemSpacing = ImVec2(8, 6);
    style.ScaleAllSizes(1.5f);
    ImGui_ImplOpenGL3_Init("#version 300 es");
    g_setup = true;
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "ImGui setup done");
}

void DrawOverlayInfo() {
    v2::FeatureState s;
    {
        std::lock_guard<std::mutex> lock(v2::StateMutex());
        s = v2::State();
    }
    v2::RuntimeInfo info;
    {
        std::lock_guard<std::mutex> lock(v2::InfoMutex());
        info = v2::Info();
    }

    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_Always);
    ImGui::Begin("##v2status", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoNav);
    ImGui::Text("v2-mod");
    ImGui::Separator();
    ImGui::Text("Hook: %s", info.hookState.c_str());
    ImGui::Text("Retri: %s (%s)", info.retriState.c_str(), info.retriDetail.c_str());
    if (s.room.enabled) {
        if (s.room.showTeam) ImGui::Text("Camp: %d", info.localCamp);
        if (s.room.showRank) ImGui::Text("Rank: %d", info.localRank);
        if (s.room.showRoomId) ImGui::Text("Room: %llu", (unsigned long long)info.roomId);
    }
    ImGui::Text("Monsters: %d  Players: %d  Enemies: %d", info.monsterCount, info.playerCount, info.enemyCount);
    ImGui::TextDisabled("Menu: %s (set menu_open in config)", s.menu.open ? "open" : "hidden");
    ImGui::End();
}

void DrawMenu() {
    v2::FeatureState s;
    {
        std::lock_guard<std::mutex> lock(v2::StateMutex());
        s = v2::State();
    }
    v2::RuntimeInfo info;
    {
        std::lock_guard<std::mutex> lock(v2::InfoMutex());
        info = v2::Info();
    }

    if (!s.menu.open) {
        DrawOverlayInfo();
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.62f, io.DisplaySize.y * 0.62f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.19f, io.DisplaySize.y * 0.18f), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("v2-mod", &s.menu.open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        std::lock_guard<std::mutex> lock(v2::StateMutex());
        v2::State().menu.open = s.menu.open;
        return;
    }

    if (ImGui::BeginTabBar("tabs")) {
        if (ImGui::BeginTabItem("Auto Retribution")) {
            ImGui::Checkbox("Enable Auto Retribution", &s.retri.enabled);
            ImGui::Checkbox("Real cast (safety gate, TODO)", &s.retri.castEnabled);
            HelpMarker("Cast invoke is not implemented yet. When enabled, it only marks cast_attempted/not_implemented after verification path is added.");
            ImGui::SliderInt("HP threshold %%", &s.retri.hpPercent, 1, 100);
            ImGui::SliderInt("Cooldown ms", &s.retri.cooldownMs, 0, 5000);
            ImGui::SliderInt("Max monster scan", &s.retri.maxScan, 1, 256);
            ImGui::Checkbox("Only wild monsters", &s.retri.onlyWild);
            ImGui::InputInt("Required summon skill ID (0=any)", &s.retri.requiredSkillId);

            ImGui::Separator();
            ImGui::Text("Target filter");
            HelpMarker("If monster IDs are configured, only matching enabled categories are targeted. If all IDs are 0, legacy wild-monster detection is used.");
            ImGui::Checkbox("Lord", &s.retri.targetLord);
            ImGui::SameLine(); ImGui::InputInt("ID##lord", &s.retri.idLord);
            ImGui::Checkbox("Turtle", &s.retri.targetTurtle);
            ImGui::SameLine(); ImGui::InputInt("ID##turtle", &s.retri.idTurtle);
            ImGui::Checkbox("Blue buff", &s.retri.targetBlueBuff);
            ImGui::SameLine(); ImGui::InputInt("ID##blue", &s.retri.idBlueBuff);
            ImGui::Checkbox("Purple buff", &s.retri.targetPurpleBuff);
            ImGui::SameLine(); ImGui::InputInt("ID##purple", &s.retri.idPurpleBuff);
            ImGui::Checkbox("Red buff", &s.retri.targetRedBuff);
            ImGui::SameLine(); ImGui::InputInt("ID##red", &s.retri.idRedBuff);

            ImGui::Separator();
            ImGui::Text("Status: %s (%s)", info.retriState.c_str(), info.retriDetail.c_str());
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Aim")) {
            ImGui::Checkbox("Enable aim assist", &s.aim.enabled);
            HelpMarker("Aim assist requires verified camera/world-to-screen/aim-write offsets for current build. Currently scaffolded; no aim write until offsets are set.");
            ImGui::Checkbox("Beatrix only", &s.aim.beatrixOnly);
            ImGui::Checkbox("Basic attack mode", &s.aim.basicAttackOnly);
            ImGui::Checkbox("Enemy only", &s.aim.targetEnemyOnly);
            ImGui::SliderFloat("FOV", &s.aim.fov, 10.0f, 500.0f, "%.0f");
            ImGui::SliderFloat("Smooth", &s.aim.smooth, 1.0f, 30.0f, "%.1f");
            ImGui::TextDisabled("Offsets needed: camera, world-to-screen, transform, aim/write target.");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("ESP")) {
            ImGui::Checkbox("Enable ESP", &s.esp.enabled);
            HelpMarker("ESP requires verified camera/world-to-screen/transform offsets. Player list uses BattleManager.m_ShowPlayers (0x70) and camp from ShowEntity.m_EntityCampType (0xD0).");
            ImGui::Checkbox("Box", &s.esp.showBox);
            ImGui::Checkbox("Line", &s.esp.showLine);
            ImGui::Checkbox("Name", &s.esp.showName);
            ImGui::Checkbox("HP", &s.esp.showHp);
            ImGui::Checkbox("Distance", &s.esp.showDistance);
            ImGui::Checkbox("Enemy only", &s.esp.enemyOnly);
            ImGui::Checkbox("Hide teammates", &s.esp.hideTeammates);
            ImGui::TextDisabled("Offsets needed: camera/main, world-to-screen, transform position.");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Room Info")) {
            ImGui::Checkbox("Show room info overlay", &s.room.enabled);
            ImGui::Checkbox("Show rank", &s.room.showRank);
            ImGui::Checkbox("Show team/camp", &s.room.showTeam);
            ImGui::Checkbox("Show room ID", &s.room.showRoomId);
            ImGui::Separator();
            ImGui::Text("Local camp: %d", info.localCamp);
            ImGui::Text("Local rank: %d", info.localRank);
            ImGui::Text("Room ID: %llu", (unsigned long long)info.roomId);
            ImGui::Text("Players: %d  Enemies: %d", info.playerCount, info.enemyCount);
            ImGui::TextDisabled("Rank/team read from ShowPlayer fields; verify on target build.");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Settings")) {
            ImGui::Checkbox("Debug log", &s.debugLog);
            ImGui::SliderInt("Config reload sec", &s.configReloadSec, 0, 30);
            ImGui::Text("Hook: %s", info.hookState.c_str());
            ImGui::Text("Detail: %s", info.hookDetail.c_str());
            ImGui::Separator();
            if (ImGui::Button("Save config")) {
                std::lock_guard<std::mutex> lock(v2::StateMutex());
                v2::State() = s;
                v2::SaveConfig();
                v2::WriteStatus();
            }
            ImGui::SameLine();
            if (ImGui::Button("Reload config")) {
                v2::LoadConfig();
                std::lock_guard<std::mutex> lock(v2::StateMutex());
                s = v2::State();
            }
            ImGui::TextDisabled("Config: /data/local/tmp/v2_autoretribution.cfg");
            ImGui::TextDisabled("Status: /data/local/tmp/v2_autoretribution.status");
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();

    {
        std::lock_guard<std::mutex> lock(v2::StateMutex());
        v2::State() = s;
    }
}

void DrawESP() {
    v2::FeatureState s;
    v2::RuntimeInfo info;
    {
        std::lock_guard<std::mutex> lock(v2::StateMutex());
        s = v2::State();
    }
    {
        std::lock_guard<std::mutex> lock(v2::InfoMutex());
        info = v2::Info();
    }
    if (!s.esp.enabled) return;
    if (!info.battleManager) return;

    void* cam = game::GetMainCamera();
    if (!cam) return;

    auto* bm = reinterpret_cast<void*>(info.battleManager);
    auto* local = reinterpret_cast<void*>(info.localPlayer);
    auto readPtr = [](void* obj, uintptr_t off) -> void* {
        if (!obj || !off) return nullptr;
        return *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(obj) + off);
    };
    auto readInt = [](void* obj, uintptr_t off) -> int {
        if (!obj) return 0;
        return *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(obj) + off);
    };
    auto readBool = [](void* obj, uintptr_t off) -> bool {
        if (!obj) return false;
        return *reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(obj) + off);
    };

    void* players = readPtr(bm, v2::GameOffsets().bm_show_players);
    if (!players) return;
    int count = readInt(players, 0x18);
    if (count < 0 || count > 32) count = (count < 0) ? 0 : 32;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImVec2 disp = ImGui::GetIO().DisplaySize;
    int localCamp = info.localCamp;

    for (int i = 0; i < count; ++i) {
        void* items = readPtr(players, 0x10);
        if (!items) break;
        void* p = readPtr(items, 0x20 + (uintptr_t)i * sizeof(void*));
        if (!p || p == local) continue;
        if (readBool(p, v2::GameOffsets().show_entity_dead)) continue;

        int camp = readInt(p, v2::GameOffsets().show_entity_camp);
        bool isEnemy = (localCamp != 0 && camp != 0 && camp != localCamp);
        if (s.esp.hideTeammates && !isEnemy) continue;
        if (s.esp.enemyOnly && !isEnemy) continue;

        game::Vec3 wp = game::GetPosition(p);
        if (wp.x == 0 && wp.y == 0 && wp.z == 0) continue;
        game::Vec3 sp{};
        if (!game::WorldToScreen(cam, wp, sp)) continue;
        if (sp.x < 0 || sp.x > disp.x || sp.y < 0 || sp.y > disp.y) continue;

        float headY = sp.y - 60.0f;
        float footY = sp.y + 10.0f;
        float w = 40.0f;
        ImU32 col = isEnemy ? IM_COL32(255, 80, 80, 220) : IM_COL32(80, 200, 255, 220);

        if (s.esp.showBox) {
            dl->AddRect(ImVec2(sp.x - w/2, headY), ImVec2(sp.x + w/2, footY), col, 2.0f, 0, 1.5f);
        }
        if (s.esp.showLine) {
            dl->AddLine(ImVec2(disp.x * 0.5f, disp.y), ImVec2(sp.x, footY), col, 1.2f);
        }
        if (s.esp.showHp) {
            int hp = readInt(p, v2::GameOffsets().show_entity_hp);
            int hpMax = readInt(p, v2::GameOffsets().show_entity_hp_max);
            if (hpMax > 0) {
                float ratio = (float)hp / (float)hpMax;
                if (ratio < 0) ratio = 0; if (ratio > 1) ratio = 1;
                float barH = (footY - headY);
                float fill = barH * ratio;
                dl->AddRectFilled(ImVec2(sp.x - w/2 - 6, headY), ImVec2(sp.x - w/2 - 2, footY), IM_COL32(40,40,40,180));
                dl->AddRectFilled(ImVec2(sp.x - w/2 - 6, footY - fill), ImVec2(sp.x - w/2 - 2, footY), IM_COL32(80,220,80,220));
            }
        }
        if (s.esp.showName) {
            char buf[64];
            snprintf(buf, sizeof(buf), "%s %d", isEnemy ? "ENEMY" : "TEAM", camp);
            dl->AddText(ImVec2(sp.x - w/2, headY - 16), col, buf);
        }
    }
}

}  // namespace v2menu
