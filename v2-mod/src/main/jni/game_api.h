#pragma once

#include <cstdint>

namespace game {

struct Vec3 { float x, y, z; };

bool Resolve(uintptr_t il2cppBase);
void* GetMainCamera();
bool WorldToScreen(void* camera, const Vec3& world, Vec3& out);
Vec3 GetPosition(void* componentOrTransform);
void* GetTransform(void* component);

}  // namespace game
