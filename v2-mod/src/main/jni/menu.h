#pragma once

#include "external_engine.h"

namespace v2menu {
void SetupImGui();
void DrawMenu();
void DrawOverlayInfo();
void DrawESP();
void DrawExternalESP(const ext::FrameData& frame);
void StartRenderHook();
}  // namespace v2menu
