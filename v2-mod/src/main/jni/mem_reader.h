#pragma once
#include <cstdint>
#include <cstddef>
#include <sys/types.h>

namespace ext {

bool initReader(pid_t pid);
void closeReader();
pid_t targetPid();
bool readMem(uintptr_t addr, void* out, size_t size);
uintptr_t getModuleBase(const char* moduleName);

template <typename T>
T read(uintptr_t addr, T def = T{}) {
    T val = def;
    if (addr) readMem(addr, &val, sizeof(T));
    return val;
}

template <typename T>
bool writeMem(uintptr_t addr, const T& val) { (void)addr; (void)val; return false; } // external read-only by default

}  // namespace ext
