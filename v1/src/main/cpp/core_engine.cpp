#include "core_engine.h"

#include <sys/system_properties.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#ifndef BUILD_TERMUX_CLI
#include <android/log.h>
#define CORE_LOG_TAG "MEOW_V1"
#define CORE_LOGI(...) __android_log_print(ANDROID_LOG_INFO, CORE_LOG_TAG, __VA_ARGS__)
#define CORE_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, CORE_LOG_TAG, __VA_ARGS__)
#else
#define CORE_LOGI(...)
#define CORE_LOGE(...)
#endif

namespace {

struct CpuSample {
    uint64_t idle = 0;
    uint64_t total = 0;
};

std::mutex g_cpuMutex;
CpuSample g_prevCpu{0, 0};

std::string readFile(const char* path) {
    std::ifstream in(path);
    if (!in.is_open()) return "";
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::string getProp(const char* key, const char* fallback = "unknown") {
#if defined(__ANDROID__)
    char value[PROP_VALUE_MAX] = {0};
    int len = __system_property_get(key, value);
    if (len > 0) return std::string(value);
#else
    (void)key;
#endif
    return std::string(fallback);
}

CpuSample readCpuSample() {
    std::ifstream in("/proc/stat");
    std::string cpu;
    uint64_t user = 0, nice = 0, system = 0, idle = 0, iowait = 0, irq = 0, softirq = 0, steal = 0;
    if (!(in >> cpu >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal)) return {};
    return {idle + iowait, user + nice + system + idle + iowait + irq + softirq + steal};
}

char* dupString(const std::string& s) {
    char* out = static_cast<char*>(malloc(s.size() + 1));
    if (!out) return nullptr;
    memcpy(out, s.c_str(), s.size() + 1);
    return out;
}

}  // namespace

namespace core {

std::string getMemInfo() {
    std::ifstream in("/proc/meminfo");
    if (!in.is_open()) return "Mem: unavailable";
    std::string key, unit;
    uint64_t value = 0, memTotal = 0, memAvailable = 0;
    while (in >> key >> value >> unit) {
        if (key == "MemTotal:") memTotal = value;
        if (key == "MemAvailable:") memAvailable = value;
    }
    if (memTotal == 0) return "Mem: unavailable";
    uint64_t memUsed = memTotal - memAvailable;
    char buf[128];
    snprintf(buf, sizeof(buf), "Mem: %llu / %llu MB",
             (unsigned long long)(memUsed / 1024),
             (unsigned long long)(memTotal / 1024));
    return std::string(buf);
}

std::string getSystemInfo() {
    const std::string abi = getProp("ro.product.cpu.abi", "unknown");
    const std::string sdk = getProp("ro.build.version.sdk", "?");
    const std::string device = getProp("ro.product.model", "Android");
    std::string kernel = readFile("/proc/version");
    if (kernel.empty()) {
        struct utsname uts {};
        if (uname(&uts) == 0) kernel = uts.release;
        else kernel = "unknown";
    } else {
        auto nl = kernel.find('\n');
        if (nl != std::string::npos) kernel = kernel.substr(0, nl);
        if (kernel.size() > 120) kernel = kernel.substr(0, 120) + "...";
        kernel = trim(kernel);
    }
    std::string uptime = trim(readFile("/proc/uptime"));
    double uptimeSec = 0.0;
    if (!uptime.empty()) uptimeSec = strtod(uptime.c_str(), nullptr);

    char buf[1024];
    snprintf(buf, sizeof(buf),
             "Device: %s\nABI: %s (SDK %s)\nKernel: %s\nUptime: %.0fs\n%s",
             device.c_str(), abi.c_str(), sdk.c_str(), kernel.c_str(), uptimeSec, getMemInfo().c_str());
    return std::string(buf);
}

std::string getCpuLoad() {
    CpuSample cur = readCpuSample();
    double cpuPercent = 0.0;
    {
        std::lock_guard<std::mutex> lock(g_cpuMutex);
        if (g_prevCpu.total != 0 && cur.total > g_prevCpu.total) {
            uint64_t totalDelta = cur.total - g_prevCpu.total;
            uint64_t idleDelta = cur.idle - g_prevCpu.idle;
            cpuPercent = (double)(totalDelta - idleDelta) * 100.0 / (double)totalDelta;
            if (cpuPercent < 0.0) cpuPercent = 0.0;
            if (cpuPercent > 100.0) cpuPercent = 100.0;
        }
        g_prevCpu = cur;
    }
    double uptimeSec = 0.0;
    std::ifstream up("/proc/uptime");
    up >> uptimeSec;
    char buf[256];
    snprintf(buf, sizeof(buf), "CPU: %.1f%%\nUptime: %.0fs\n%s", cpuPercent, uptimeSec, getMemInfo().c_str());
    return std::string(buf);
}

std::string getThermalInfo() {
    std::string out;
    DIR* dir = opendir("/sys/class/thermal");
    if (!dir) return "Thermal: unavailable";
    struct dirent* ent;
    int count = 0;
    while ((ent = readdir(dir)) != nullptr) {
        if (strncmp(ent->d_name, "thermal_zone", 12) != 0) continue;
        std::string base = std::string("/sys/class/thermal/") + ent->d_name;
        std::string type = trim(readFile((base + "/type").c_str()));
        std::string tempRaw = trim(readFile((base + "/temp").c_str()));
        if (tempRaw.empty()) continue;
        long tempMilli = strtol(tempRaw.c_str(), nullptr, 10);
        double tempC = tempMilli > 1000 ? (tempMilli / 1000.0) : (double)tempMilli;
        char line[256];
        snprintf(line, sizeof(line), "%s (%s): %.1f C", ent->d_name, type.empty() ? "unknown" : type.c_str(), tempC);
        if (!out.empty()) out += "\n";
        out += line;
        if (++count >= 12) break;
    }
    closedir(dir);
    if (out.empty()) return "Thermal: no readable zones";
    return out;
}

std::string getSelinuxStatus() {
    // Read-only: use getenforce command output; no writes.
    FILE* fp = popen("getenforce 2>&1", "r");
    if (!fp) return "SELinux: unavailable";
    char buf[128] = {0};
    std::string out;
    while (fgets(buf, sizeof(buf), fp)) out += buf;
    pclose(fp);
    out = trim(out);
    if (out.empty()) out = "unknown";
    return "SELinux: " + out;
}

std::string formatCommandOutput(const std::string& raw) {
    std::istringstream iss(raw);
    std::string line;
    std::ostringstream out;
    int lines = 0;
    while (std::getline(iss, line)) {
        std::string t = trim(line);
        if (t.empty()) continue;
        if (lines > 0) out << "\n";
        out << "> " << t;
        ++lines;
        if (lines >= 120) {
            out << "\n> ... (truncated)";
            break;
        }
    }
    if (lines == 0) out << "> (no output)";
    return out.str();
}

}  // namespace core

extern "C" {

__attribute__((visibility("default"))) const char* core_get_system_info(void) { return dupString(core::getSystemInfo()); }
__attribute__((visibility("default"))) const char* core_get_cpu_load(void) { return dupString(core::getCpuLoad()); }
__attribute__((visibility("default"))) const char* core_get_mem_info(void) { return dupString(core::getMemInfo()); }
__attribute__((visibility("default"))) const char* core_get_thermal_info(void) { return dupString(core::getThermalInfo()); }
__attribute__((visibility("default"))) const char* core_get_selinux_status(void) { return dupString(core::getSelinuxStatus()); }
__attribute__((visibility("default"))) const char* core_format_command_output(const char* raw) { return dupString(core::formatCommandOutput(raw ? std::string(raw) : "")); }
__attribute__((visibility("default"))) void core_free_string(const char* str) { free(const_cast<char*>(str)); }

}  // extern "C"
