#include "mem_reader.h"

#include <android/log.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <unistd.h>

#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

#define LOG_TAG "MEOW_V2EXT"

namespace ext {
namespace {
pid_t g_pid = -1;
int g_memFd = -1;
}  // namespace

bool initReader(pid_t pid) {
    closeReader();
    g_pid = pid;
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/mem", (int)pid);
    g_memFd = open(path, O_RDONLY);
    if (g_memFd < 0) {
        __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "open %s failed: %s", path, strerror(errno));
        g_pid = -1;
        return false;
    }
    return true;
}

void closeReader() {
    if (g_memFd >= 0) { close(g_memFd); g_memFd = -1; }
    g_pid = -1;
}

pid_t targetPid() { return g_pid; }

bool readMem(uintptr_t addr, void* out, size_t size) {
    if (!addr || !out || size == 0 || g_pid <= 0) return false;
#if defined(__aarch64__) || defined(__arm__) || defined(__i386__) || defined(__x86_64__)
    struct iovec local{out, size};
    struct iovec remote{reinterpret_cast<void*>(addr), size};
    ssize_t n = process_vm_readv(g_pid, &local, 1, &remote, 1, 0);
    if (n == (ssize_t)size) return true;
#endif
    if (g_memFd >= 0) {
        ssize_t n = pread64(g_memFd, out, size, (off64_t)addr);
        if (n == (ssize_t)size) return true;
    }
    return false;
}

uintptr_t getModuleBase(const char* moduleName) {
    if (g_pid <= 0 || !moduleName) return 0;
    char mapsPath[64];
    snprintf(mapsPath, sizeof(mapsPath), "/proc/%d/maps", (int)g_pid);
    std::ifstream maps(mapsPath);
    std::string line;
    uintptr_t base = 0;
    while (std::getline(maps, line)) {
        if (line.find(moduleName) == std::string::npos) continue;
        uintptr_t start = 0;
        if (sscanf(line.c_str(), "%lx-%*lx", &start) == 1 || sscanf(line.c_str(), "%" SCNxPTR "-%*x", &start) == 1) {
            if (base == 0 || start < base) base = start;
        }
    }
    if (base) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "module %s base=%" PRIxPTR, moduleName, base);
    return base;
}

}  // namespace ext
