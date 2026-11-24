#include <dlfcn.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

namespace {

using GetStrFn = const char* (*)();
using FreeFn = void (*)(const char*);

struct CoreApi {
    void* handle = nullptr;
    GetStrFn get_system_info = nullptr;
    GetStrFn get_cpu_load = nullptr;
    GetStrFn get_thermal_info = nullptr;
    GetStrFn get_selinux_status = nullptr;
    FreeFn free_string = nullptr;
};

bool loadCoreApi(CoreApi& api) {
    const char* candidates[] = {"./libnativecore_cli.so", "libnativecore_cli.so"};
    for (const char* path : candidates) {
        api.handle = dlopen(path, RTLD_NOW);
        if (api.handle) break;
    }
    if (!api.handle) {
        std::cerr << "dlopen failed: " << dlerror() << std::endl;
        return false;
    }
    api.get_system_info = reinterpret_cast<GetStrFn>(dlsym(api.handle, "core_get_system_info"));
    api.get_cpu_load = reinterpret_cast<GetStrFn>(dlsym(api.handle, "core_get_cpu_load"));
    api.get_thermal_info = reinterpret_cast<GetStrFn>(dlsym(api.handle, "core_get_thermal_info"));
    api.get_selinux_status = reinterpret_cast<GetStrFn>(dlsym(api.handle, "core_get_selinux_status"));
    api.free_string = reinterpret_cast<FreeFn>(dlsym(api.handle, "core_free_string"));
    if (!api.get_system_info || !api.get_cpu_load || !api.get_thermal_info || !api.get_selinux_status || !api.free_string) {
        std::cerr << "dlsym failed: " << dlerror() << std::endl;
        dlclose(api.handle);
        api = {};
        return false;
    }
    return true;
}

std::string takeString(CoreApi& api, const char* s) {
    if (!s) return "";
    std::string out = s;
    api.free_string(s);
    return out;
}

void clearScreen() {
    std::cout << "\033[2J\033[H";
}

}  // namespace

int main() {
    CoreApi api;
    if (!loadCoreApi(api)) return 1;

    while (true) {
        clearScreen();
        std::cout << "=== v1 Read-Only Telemetry (Termux) ===\n\n";
        std::cout << takeString(api, api.get_system_info()) << "\n\n";
        std::cout << "--- CPU/RAM ---\n" << takeString(api, api.get_cpu_load()) << "\n\n";
        std::cout << "--- SELinux ---\n" << takeString(api, api.get_selinux_status()) << "\n\n";
        std::cout << "--- Thermal ---\n" << takeString(api, api.get_thermal_info()) << "\n\n";
        std::cout << "Refresh: 2s | Ctrl+C to exit\n";
        std::cout.flush();
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    dlclose(api.handle);
    return 0;
}
