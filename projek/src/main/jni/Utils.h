#ifndef UTILS_H
#define UTILS_H

#include <jni.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <elf.h>

typedef unsigned long DWORD;
static uintptr_t libBase;

DWORD findLibrary(const char *library);

DWORD getAbsoluteAddress(const char *libraryName, DWORD relativeAddr);

bool isLibraryLoaded(const char *libraryName);

DWORD findLibrary(const char *library) {
    char buffer[1024] = {0};
    FILE *fp = fopen("/proc/self/maps", "rt");
    if (!fp) return 0;

    DWORD address = 0;
    while (fgets(buffer, sizeof(buffer), fp)) {
        if (!strstr(buffer, library)) continue;

        unsigned long long start = 0, end = 0, offset = 0;
        char perms[8] = {0};
        if (sscanf(buffer, "%llx-%llx %7s %llx", &start, &end, perms, &offset) != 4) continue;
        if (offset != 0) continue;

        address = static_cast<DWORD>(start);
        Elf64_Ehdr ehdr;
        if (memcpy(&ehdr, reinterpret_cast<const void *>(start), sizeof(ehdr)) != &ehdr) break;
        if (memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0) break;

        for (int i = 0; i < ehdr.e_phnum; ++i) {
            const void *ph = reinterpret_cast<const void *>(start + ehdr.e_phoff + static_cast<unsigned>(i) * ehdr.e_phentsize);
            Elf64_Phdr phdr;
            if (memcpy(&phdr, ph, sizeof(phdr)) != &phdr) continue;
            if (phdr.p_type == PT_LOAD && phdr.p_offset == 0) {
                address = static_cast<DWORD>(start - phdr.p_vaddr);
                break;
            }
        }
        break;
    }

    fclose(fp);
    return address;
}

DWORD getAbsoluteAddress(const char *libraryName, DWORD relativeAddr) {
    libBase = findLibrary(libraryName);
    if (libBase == 0)
        return 0;
    return (reinterpret_cast<DWORD>(libBase + relativeAddr));
}

bool isLibraryLoaded(const char *libraryName) {
    char line[512] = {0};
    FILE *fp = fopen("/proc/self/maps", "rt");
    if (fp != NULL) {
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, libraryName))
                return true;
        }
        fclose(fp);
    }
    return false;
}

#endif