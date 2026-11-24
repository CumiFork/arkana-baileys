#include "game_api.h"

#include <android/log.h>
#include <dlfcn.h>
#include <pthread.h>
#include <mutex>

#include "xdl.h"

#define LOG_TAG "MEOW_V2"

namespace game {
namespace {

std::mutex g_mu;
bool g_resolved = false;
uintptr_t g_base = 0;

typedef void* (*Il2CppClassFromNameFn)(const void* image, const char* namespaze, const char* name);
typedef void* (*Il2CppClassGetMethodFromNameFn)(void* klass, const char* name, int argsCount);
typedef void* (*Il2CppRuntimeInvokeFn)(void* method, void* obj, void** params, void** exc);
typedef void* (*Il2CppDomainGetFn)();
typedef void** (*Il2CppDomainGetAssembliesFn)(const void* domain, size_t* size);
typedef const void* (*Il2CppAssemblyGetImageFn)(void* assembly);
typedef void* (*Il2CppThreadAttachFn)(void* domain);

Il2CppClassFromNameFn il2cpp_class_from_name = nullptr;
Il2CppClassGetMethodFromNameFn il2cpp_class_get_method_from_name = nullptr;
Il2CppRuntimeInvokeFn il2cpp_runtime_invoke = nullptr;
Il2CppDomainGetFn il2cpp_domain_get = nullptr;
Il2CppDomainGetAssembliesFn il2cpp_domain_get_assemblies = nullptr;
Il2CppAssemblyGetImageFn il2cpp_assembly_get_image = nullptr;
Il2CppThreadAttachFn il2cpp_thread_attach = nullptr;

void* m_getMain = nullptr;
void* m_getTransform = nullptr;
void* m_getPosition = nullptr;
void* m_worldToScreen = nullptr; // Camera.WorldToScreenPoint(Vector3)

using W2SStaticFn = float (*)(void* cam, float x, float y, float z, float* oy, float* oz);
W2SStaticFn w2s_static = nullptr;

void* findClass(const char* ns, const char* name) {
    if (!il2cpp_class_from_name) return nullptr;
    if (il2cpp_domain_get && il2cpp_domain_get_assemblies && il2cpp_assembly_get_image) {
        void* domain = il2cpp_domain_get();
        size_t n = 0;
        void** asms = il2cpp_domain_get_assemblies(domain, &n);
        for (size_t i = 0; i < n; ++i) {
            const void* img = il2cpp_assembly_get_image(asms[i]);
            if (!img) continue;
            void* k = il2cpp_class_from_name(img, ns, name);
            if (k) return k;
        }
    }
    return il2cpp_class_from_name(nullptr, ns, name);
}

}  // namespace

bool Resolve(uintptr_t il2cppBase) {
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_resolved) return true;
    g_base = il2cppBase;

    void* h = xdl_open("libil2cpp.so", XDL_DEFAULT);
    if (!h) return false;
    il2cpp_class_from_name = (Il2CppClassFromNameFn)xdl_sym(h, "il2cpp_class_from_name", nullptr);
    il2cpp_class_get_method_from_name = (Il2CppClassGetMethodFromNameFn)xdl_sym(h, "il2cpp_class_get_method_from_name", nullptr);
    il2cpp_runtime_invoke = (Il2CppRuntimeInvokeFn)xdl_sym(h, "il2cpp_runtime_invoke", nullptr);
    il2cpp_domain_get = (Il2CppDomainGetFn)xdl_sym(h, "il2cpp_domain_get", nullptr);
    il2cpp_domain_get_assemblies = (Il2CppDomainGetAssembliesFn)xdl_sym(h, "il2cpp_domain_get_assemblies", nullptr);
    il2cpp_assembly_get_image = (Il2CppAssemblyGetImageFn)xdl_sym(h, "il2cpp_assembly_get_image", nullptr);
    il2cpp_thread_attach = (Il2CppThreadAttachFn)xdl_sym(h, "il2cpp_thread_attach", nullptr);

    if (il2cpp_thread_attach && il2cpp_domain_get) il2cpp_thread_attach(il2cpp_domain_get());

    void* cameraK = findClass("UnityEngine", "Camera");
    void* componentK = findClass("UnityEngine", "Component");
    void* transformK = findClass("UnityEngine", "Transform");

    if (cameraK) {
        m_getMain = il2cpp_class_get_method_from_name(cameraK, "get_main", 0);
        m_worldToScreen = il2cpp_class_get_method_from_name(cameraK, "WorldToScreenPoint", 1);
    }
    if (componentK) {
        m_getTransform = il2cpp_class_get_method_from_name(componentK, "get_transform", 0);
    }
    if (transformK) {
        m_getPosition = il2cpp_class_get_method_from_name(transformK, "get_position", 0);
    }

    // Static helper from dump: WorldToScreenPoint(Camera, x,y,z, &y,&z) RVA 0x22E993C
    if (il2cppBase) {
        w2s_static = (W2SStaticFn)(il2cppBase + 0x22E993C);
    }

    g_resolved = (m_getMain || w2s_static) && (m_getTransform != nullptr) && (m_getPosition != nullptr);
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "GameAPI resolve: main=%p w2s=%p w2sStatic=%p getTr=%p getPos=%p ok=%d",
                        m_getMain, m_worldToScreen, (void*)w2s_static, m_getTransform, m_getPosition, g_resolved);
    return g_resolved;
}

void* GetMainCamera() {
    if (!m_getMain || !il2cpp_runtime_invoke) return nullptr;
    void* exc = nullptr;
    void* ret = il2cpp_runtime_invoke(m_getMain, nullptr, nullptr, &exc);
    if (exc) return nullptr;
    return ret;
}

Vec3 GetPosition(void* componentOrTransform) {
    Vec3 out{0,0,0};
    if (!componentOrTransform || !m_getPosition || !il2cpp_runtime_invoke) return out;
    void* obj = componentOrTransform;
    // If it's a component, get transform first
    if (m_getTransform) {
        void* exc = nullptr;
        void* tr = il2cpp_runtime_invoke(m_getTransform, componentOrTransform, nullptr, &exc);
        if (!exc && tr) obj = tr;
    }
    void* exc = nullptr;
    void* ret = il2cpp_runtime_invoke(m_getPosition, obj, nullptr, &exc);
    if (exc || !ret) return out;
    // boxed Vector3: x,y,z at +0x10
    float* f = reinterpret_cast<float*>((uintptr_t)ret + 0x10);
    out.x = f[0]; out.y = f[1]; out.z = f[2];
    return out;
}

void* GetTransform(void* component) {
    if (!component || !m_getTransform || !il2cpp_runtime_invoke) return nullptr;
    void* exc = nullptr;
    void* ret = il2cpp_runtime_invoke(m_getTransform, component, nullptr, &exc);
    if (exc) return nullptr;
    return ret;
}

bool WorldToScreen(void* camera, const Vec3& world, Vec3& out) {
    if (!camera) return false;

    // Try static helper first (has RVA in dump)
    if (w2s_static) {
        float sy = 0, sz = 0;
        float sx = w2s_static(camera, world.x, world.y, world.z, &sy, &sz);
        out.x = sx; out.y = sy; out.z = sz;
        return out.z > 0.01f;
    }

    if (!m_worldToScreen || !il2cpp_runtime_invoke) return false;
    void* exc = nullptr;
    void* params[1] = { (void*)&world };
    void* ret = il2cpp_runtime_invoke(m_worldToScreen, camera, params, &exc);
    if (exc || !ret) return false;
    float* f = reinterpret_cast<float*>((uintptr_t)ret + 0x10);
    out.x = f[0]; out.y = f[1]; out.z = f[2];
    return out.z > 1.0f;
}

}  // namespace game
