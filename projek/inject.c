#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/uio.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>
#include <dirent.h>
#include <elf.h>
#include <fcntl.h>

static int ptrace_attach(pid_t pid) {
    if (ptrace(PTRACE_ATTACH, pid, NULL, NULL) < 0) return -1;
    int st;
    if (waitpid(pid, &st, 0) < 0) return -1;
    return 0;
}
static int ptrace_detach(pid_t pid) { return ptrace(PTRACE_DETACH, pid, NULL, NULL); }

static unsigned long remote_base(pid_t pid, const char *name) {
    char path[64], line[512];
    snprintf(path, sizeof(path), "/proc/%d/maps", pid);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    unsigned long base = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, name) && (strstr(line, "r-xp") || strstr(line, "r--p"))) {
            base = strtoul(line, NULL, 16);
            break;
        }
    }
    fclose(f);
    return base;
}

static unsigned long local_base(const char *name) { return remote_base(getpid(), name); }

static unsigned long remote_dlopen(pid_t pid) {
    // Try libc.so then libdl.so; on Android dlopen is often in libc
    const char *libs[] = {"/apex/com.android.runtime/lib64/bionic/libdl.so", "/system/lib64/libdl.so", "libdl.so", "libc.so", NULL};
    void *h = dlopen("libdl.so", RTLD_NOW);
    void *sym = dlsym(h, "dlopen");
    if (!sym) sym = dlsym(RTLD_DEFAULT, "dlopen");
    if (!sym) { fprintf(stderr, "[!] local dlopen not found\n"); return 0; }
    unsigned long ls = (unsigned long)sym;
    for (int i = 0; libs[i]; i++) {
        unsigned long lb = local_base(libs[i]);
        unsigned long rb = remote_base(pid, libs[i]);
        if (lb && rb) {
            // verify sym is in this lib? rough: check range by maps
            char path[64], line[512];
            snprintf(path, sizeof(path), "/proc/self/maps");
            FILE *f = fopen(path, "r");
            if (!f) continue;
            unsigned long start=0,end=0;(void)end;
            int found=0;
            while (fgets(line, sizeof(line), f)) {
                if (!strstr(line, libs[i])) continue;
                unsigned long s,e; char perms[5];
                if (sscanf(line, "%lx-%lx %4s", &s, &e, perms) == 3) {
                    if (!start) start=s; end=e;
                    if (ls >= s && ls < e) found=1;
                }
            }
            fclose(f);
            if (found) {
                fprintf(stderr, "[+] dlopen lib=%s local=%lx remote=%lx\n", libs[i], lb, rb);
                return rb + (ls - lb);
            }
        }
    }
    // fallback: assume libc
    unsigned long lb = local_base("libc.so");
    unsigned long rb = remote_base(pid, "libc.so");
    if (lb && rb) return rb + (ls - lb);
    return 0;
}

static int remote_call(pid_t pid, unsigned long fn, unsigned long *args, int nargs, unsigned long *ret) {
    struct user_regs_struct regs, old;
    if (ptrace(PTRACE_GETREGSET, pid, (void*)1, &(struct iovec){&regs, sizeof(regs)}) < 0) return -1;
    old = regs;
    for (int i = 0; i < nargs && i < 8; i++) regs.regs[i] = args[i];
    regs.pc = fn;
    regs.regs[30] = 0; // lr
    // align sp
    regs.sp &= ~0xful;
    if (ptrace(PTRACE_SETREGSET, pid, (void*)1, &(struct iovec){&regs, sizeof(regs)}) < 0) return -1;
    if (ptrace(PTRACE_CONT, pid, NULL, NULL) < 0) return -1;
    int st;
    waitpid(pid, &st, 0);
    if (WIFSTOPPED(st)) {
        struct user_regs_struct after;
        ptrace(PTRACE_GETREGSET, pid, (void*)1, &(struct iovec){&after, sizeof(after)});
        if (ret) *ret = after.regs[0];
    } else {
        fprintf(stderr, "[!] unexpected wait status %x\n", st);
        ptrace(PTRACE_SETREGSET, pid, (void*)1, &(struct iovec){&old, sizeof(old)});
        return -1;
    }
    if (ptrace(PTRACE_SETREGSET, pid, (void*)1, &(struct iovec){&old, sizeof(old)}) < 0) return -1;
    return 0;
}

static unsigned long remote_write_str(pid_t pid, const char *s) {
    size_t len = strlen(s) + 1;
    // write to stack area: get sp and write below it
    struct user_regs_struct regs;
    ptrace(PTRACE_GETREGSET, pid, (void*)1, &(struct iovec){&regs, sizeof(regs)});
    unsigned long addr = (regs.sp - 0x100 - len) & ~0xful;
    for (size_t i = 0; i < len; i += sizeof(long)) {
        long word = 0;
        memcpy(&word, s + i, (len - i) < sizeof(long) ? (len - i) : sizeof(long));
        if (ptrace(PTRACE_POKETEXT, pid, (void*)(addr + i), (void*)word) < 0) return 0;
    }
    return addr;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <pid> <path-to-so>\n", argv[0]);
        return 2;
    }
    pid_t pid = atoi(argv[1]);
    const char *so = argv[2];
    if (ptrace_attach(pid) < 0) { perror("[!] attach"); return 1; }
    fprintf(stderr, "[+] attached %d\n", pid);
    unsigned long dl = remote_dlopen(pid);
    if (!dl) { fprintf(stderr, "[!] remote dlopen not found\n"); ptrace_detach(pid); return 1; }
    fprintf(stderr, "[+] remote dlopen=%lx\n", dl);
    unsigned long str = remote_write_str(pid, so);
    if (!str) { fprintf(stderr, "[!] remote write failed\n"); ptrace_detach(pid); return 1; }
    unsigned long args[] = {str, 2 /*RTLD_NOW*/};
    unsigned long ret = 0;
    if (remote_call(pid, dl, args, 2, &ret) < 0) {
        fprintf(stderr, "[!] remote call failed\n"); ptrace_detach(pid); return 1;
    }
    fprintf(stderr, "[+] dlopen returned %lx\n", ret);
    ptrace_detach(pid);
    if (ret == 0) {
        fprintf(stderr, "[!] dlopen returned NULL (check /proc/%d/maps and logcat for dlerror)\n", pid);
        return 1;
    }
    fprintf(stderr, "[+] injected: %s\n", so);
    return 0;
}
