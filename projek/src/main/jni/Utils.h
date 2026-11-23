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

    uintptr_t execStart = 0, execOffset = 0, execSize = 0;
    while (fgets(buffer, sizeof(buffer), fp)) {
        if (!strstr(buffer, library)) continue;
        unsigned long long start = 0, end = 0, offset = 0;
        char perms[8] = {0};
        if (sscanf(buffer, "%llx-%llx %7s %llx", &start, &end, perms, &offset) != 4) continue;
        if (offset == 0 || !strchr(perms, 'x')) continue;
        if (static_cast<uintptr_t>(end - start) <= execSize) continue;
        execStart = static_cast<uintptr_t>(start);
        execOffset = static_cast<uintptr_t>(offset);
        execSize = static_cast<uintptr_t>(end - start);
    }

    uintptr_t headerStart = 0;
    rewind(fp);
    while (fgets(buffer, sizeof(buffer), fp)) {
        if (!strstr(buffer, library)) continue;
        unsigned long long start = 0, end = 0, offset = 0;
        char perms[8] = {0};
        if (sscanf(buffer, "%llx-%llx %7s %llx", &start, &end, perms, &offset) != 4) continue;
        if (offset != 0) continue;
        if (execStart && start > execStart) continue;
        if (start > headerStart) headerStart = static_cast<uintptr_t>(start);
    }
    fclose(fp);

    if (!headerStart) return 0;
    if (!execStart) return static_cast<DWORD>(headerStart);

    Elf64_Ehdr ehdr;
    if (memcpy(&ehdr, reinterpret_cast<const void *>(headerStart), sizeof(ehdr)) != &ehdr) return static_cast<DWORD>(headerStart);
    if (memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0) return static_cast<DWORD>(headerStart);

    for (int i = 0; i < ehdr.e_phnum; ++i) {
        const void *ph = reinterpret_cast<const void *>(headerStart + ehdr.e_phoff + static_cast<unsigned>(i) * ehdr.e_phentsize);
        Elf64_Phdr phdr;
        if (memcpy(&phdr, ph, sizeof(phdr)) != &phdr) continue;
        if (!(phdr.p_flags & PF_X)) continue;
        if (execOffset < phdr.p_offset || execOffset >= phdr.p_offset + phdr.p_filesz) continue;
        uintptr_t delta = static_cast<uintptr_t>(phdr.p_vaddr - phdr.p_offset);
        return static_cast<DWORD>(execStart - execOffset - delta);
    }

    return static_cast<DWORD>(headerStart);
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