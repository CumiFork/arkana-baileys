#pragma once
#include <cstdint>
#include <string>
#include "feature_state.h"
#include "game_api.h"

namespace ext {

struct FrameData {
    float view[16]{};
    float proj[16]{};
    float vp[16]{};
    bool hasMatrix = false;
    int width = 0;
    int height = 0;
    uintptr_t camera = 0;
    uintptr_t battleManager = 0;
    uintptr_t localPlayer = 0;
    int localCamp = 0;
    int localRank = 0;
    uint64_t roomId = 0;
    int playerCount = 0;
    int enemyCount = 0;
    std::string status = "init";
    std::string detail = "";
};

bool Start();
void Stop();
FrameData Snapshot();
bool WorldToScreen(const FrameData& f, const game::Vec3& world, game::Vec3& out);

}  // namespace ext
