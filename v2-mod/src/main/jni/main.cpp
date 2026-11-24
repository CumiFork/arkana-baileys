#include <android/log.h>
#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

#include "auto_retribution.h"
#include "menu.h"
#include "feature_state.h"

#define LOG_TAG "MEOW_V2"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

uintptr_t findLibraryBase(const char* name) {
    std::ifstream maps("/proc/self/maps");
    std::string line;
    uintptr_t base = 0;
    while (std::getline(maps, line)) {
        if (line.find(name) == std::string::npos) continue;
        uintptr_t start = 0;
        if (sscanf(line.c_str(), "%" SCNxPTR "-%*x", &start) == 1) {
            if (base == 0 || start < base) base = start;
        }
    }
    return base;
}

void loadConfig() {
    AutoRetribution::LoadConfigFromFile("/data/local/tmp/v2_autoretribution.cfg");
}

void* init_thread(void*) {
    LOGI("init_thread started");
    AutoRetribution::WriteStatus("init", "started");
    loadConfig();
    v2menu::StartRenderHook();

    const char* lib = "libil2cpp.so";
    uintptr_t base = 0;
    for (int i = 0; i < 120; ++i) {
        base = findLibraryBase(lib);
        if (base != 0) break;
        sleep(1);
    }

    if (base == 0) {
        LOGE("libil2cpp.so not found");
        AutoRetribution::WriteStatus("init_failed", "libil2cpp_not_found");
        return nullptr;
    }

    LOGI("libil2cpp.so base=%" PRIxPTR, base);
    AutoRetribution::WriteStatus("init", "libil2cpp_found");
    AutoRetribution::InstallHooks(base);
    return nullptr;
}

}  // namespace

__attribute__((constructor))
void lib_main() {
    pthread_t t;
    if (pthread_create(&t, nullptr, init_thread, nullptr) == 0) {
        pthread_detach(t);
    } else {
        LOGE("pthread_create failed");
    }
}
