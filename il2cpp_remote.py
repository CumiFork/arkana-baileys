#!/usr/bin/env python3
# language: Python 3, file: il2cpp_remote.py, target: rooted Android ARM64 / Termux
# ptrace remote-call engine: invokes IL2CPP methods by RVA inside a running game, returns register values.
# used to call GameFacade / Player / Transform accessors resolved from dump.cs — no injected library.

import ctypes
import os
import signal
import struct
import time
from typing import Any, Dict, List, Optional, Tuple

libc = ctypes.CDLL("libc.so", use_errno=True)
libc.ptrace.restype = ctypes.c_long
libc.ptrace.argtypes = [ctypes.c_long, ctypes.c_long, ctypes.c_void_p, ctypes.c_void_p]

PTRACE_TRACEME = 0
PTRACE_PEEKTEXT = 1
PTRACE_PEEKDATA = 2
PTRACE_POKETEXT = 4
PTRACE_POKEDATA = 5
PTRACE_CONT = 7
PTRACE_ATTACH = 16
PTRACE_DETACH = 17
PTRACE_GETREGSET = 0x4204
PTRACE_SETREGSET = 0x4205
NT_PRSTATUS = 1
NT_FPREGSET = 2

TRAP_PC = 0x111111111110  # kept inside 48-bit VA space: kernel strips higher tag bits on fault
STACK_RESERVE = 0x1000


class PtraceError(Exception):
    pass


def ptrace(req: int, tid: int, addr: int = 0, data: int = 0) -> int:
    result = libc.ptrace(req, tid, ctypes.c_void_p(addr), ctypes.c_void_p(data))
    if result == -1:
        errno = ctypes.get_errno()
        raise PtraceError(f"ptrace({req:#x}, {tid}) errno={errno} {os.strerror(errno)}")
    return result


class UserPtRegs(ctypes.Structure):
    # struct user_pt_regs from asm/ptrace.h (ARM64)
    _fields_ = [
        ("regs", ctypes.c_uint64 * 31),
        ("sp", ctypes.c_uint64),
        ("pc", ctypes.c_uint64),
        ("pstate", ctypes.c_uint64),
    ]


class Iovec(ctypes.Structure):
    _fields_ = [("iov_base", ctypes.c_void_p), ("iov_len", ctypes.c_size_t)]


class FpsimdState(ctypes.Structure):
    # struct user_fpsimd_state: v0-v31 (128-bit), fpsr, fpcr, reserved
    _fields_ = [
        ("vregs", ctypes.c_uint64 * 64),
        ("fpsr", ctypes.c_uint32),
        ("fpcr", ctypes.c_uint32),
        ("reserved", ctypes.c_uint32 * 2),
    ]


def get_regs(tid: int) -> UserPtRegs:
    regs = UserPtRegs()
    iov = Iovec(ctypes.addressof(regs), ctypes.sizeof(regs))
    ptrace(PTRACE_GETREGSET, tid, NT_PRSTATUS, ctypes.addressof(iov))
    return regs


def set_regs(tid: int, regs: UserPtRegs) -> None:
    iov = Iovec(ctypes.addressof(regs), ctypes.sizeof(regs))
    ptrace(PTRACE_SETREGSET, tid, NT_PRSTATUS, ctypes.addressof(iov))


def get_fp_regs(tid: int) -> FpsimdState:
    fp = FpsimdState()
    iov = Iovec(ctypes.addressof(fp), ctypes.sizeof(fp))
    ptrace(PTRACE_GETREGSET, tid, NT_FPREGSET, ctypes.addressof(iov))
    return fp


def set_fp_regs(tid: int, fp: FpsimdState) -> None:
    iov = Iovec(ctypes.addressof(fp), ctypes.sizeof(fp))
    ptrace(PTRACE_SETREGSET, tid, NT_FPREGSET, ctypes.addressof(iov))


def f32_from_v(fp: FpsimdState, reg: int, lane: int) -> float:
    word = int(fp.vregs[reg * 2 + lane // 2])
    bits = (word >> (32 * (lane % 2))) & 0xFFFFFFFF
    return struct.unpack("<f", struct.pack("<I", bits))[0]


def set_v_f32(fp: FpsimdState, reg: int, lane: int, value: float) -> None:
    word_index = reg * 2 + lane // 2
    shift = 32 * (lane % 2)
    bits = struct.unpack("<I", struct.pack("<f", value))[0]
    mask = 0xFFFFFFFF << shift
    fp.vregs[word_index] = (int(fp.vregs[word_index]) & ~mask) | (bits << shift)


def wait_stop(tid: int, timeout: float = 3.0) -> int:
    deadline = time.time() + timeout
    while time.time() < deadline:
        done, status = os.waitpid(tid, os.WNOHANG)
        if done == tid:
            return status
        time.sleep(0.001)
    raise PtraceError(f"timeout waiting for tid {tid}")


def u64_from_regs(regs: UserPtRegs, index: int) -> int:
    return int(regs.regs[index])


def qword_from_x01(regs: UserPtRegs) -> int:
    """16-byte composite return: x0 low, x1 high."""
    return u64_from_regs(regs, 0) | (u64_from_regs(regs, 1) << 64)


class Il2CppRemote:
    """Attach to one thread and invoke IL2CPP methods at base+RVA via ptrace."""

    def __init__(self, tid: int, base: int):
        self.tid = tid
        self.base = base
        self._saved: Optional[UserPtRegs] = None
        self._saved_fp: Optional[FpsimdState] = None
        self._attached = False

    def attach(self) -> "Il2CppRemote":
        if self._attached:
            return self
        try:
            ptrace(PTRACE_ATTACH, self.tid)
        except PtraceError as exc:
            if "errno=1" in str(exc):
                os.system("setenforce 0 >/dev/null 2>&1")
                try:
                    ptrace(PTRACE_ATTACH, self.tid)
                except PtraceError:
                    raise PtraceError(
                        f"{exc} (hint: jalankan sebagai root dan pastikan SELinux permissive: su -c 'setenforce 0')"
                    ) from exc
            else:
                raise
        # PTRACE_ATTACH already installed us as tracer. Set the flag before
        # wait_stop() so a timeout cannot leave the target permanently traced.
        self._attached = True
        try:
            wait_stop(self.tid)
            self._saved = get_regs(self.tid)
            self._saved_fp = get_fp_regs(self.tid)
        except Exception:
            try:
                ptrace(PTRACE_DETACH, self.tid)
            except PtraceError:
                pass
            self._attached = False
            self._saved = None
            self._saved_fp = None
            raise
        return self

    def detach(self) -> None:
        if not self._attached or self._saved is None:
            return
        try:
            set_regs(self.tid, self._saved)
            if self._saved_fp is not None:
                set_fp_regs(self.tid, self._saved_fp)
        finally:
            try:
                ptrace(PTRACE_DETACH, self.tid)
            except PtraceError:
                pass
            self._attached = False

    def __enter__(self) -> "Il2CppRemote":
        return self.attach()

    def __exit__(self, *exc: Any) -> None:
        self.detach()

    def call(self, rva: int, args: Optional[List[int]] = None,
             method_info: int = 0, instance: Optional[int] = None,
             fp_args: Optional[List[Tuple[int, int, float]]] = None) -> UserPtRegs:
        """Call base+rva. For instance methods pass instance in x0; MethodInfo goes last."""
        if not self._attached or self._saved is None:
            raise PtraceError("not attached")
        args = list(args or [])
        regs = UserPtRegs()
        ctypes.memmove(ctypes.addressof(regs), ctypes.addressof(self._saved), ctypes.sizeof(regs))

        regs.pc = self.base + rva
        regs.regs[30] = TRAP_PC  # LR trap: function return lands on unmapped PC -> SIGSEGV
        regs.sp = (self._saved.sp - STACK_RESERVE) & ~0xF

        # IL2CPP generated abi: instance -> x0, explicit args -> x1.., MethodInfo* last
        slot = 0
        if instance is not None:
            regs.regs[slot] = instance & 0xFFFFFFFFFFFFFFFF
            slot += 1
        for value in args:
            if slot > 7:
                raise PtraceError("more than 8 register arguments — stack args not implemented")
            regs.regs[slot] = value & 0xFFFFFFFFFFFFFFFF
            slot += 1
        if slot > 7:
            raise PtraceError("MethodInfo slot overflow")
        regs.regs[slot] = method_info & 0xFFFFFFFFFFFFFFFF

        if self._saved_fp is not None:
            fp = FpsimdState()
            ctypes.memmove(ctypes.addressof(fp), ctypes.addressof(self._saved_fp), ctypes.sizeof(fp))
            for reg, lane, value in fp_args or []:
                set_v_f32(fp, reg, lane, value)
            set_fp_regs(self.tid, fp)
        set_regs(self.tid, regs)
        ptrace(PTRACE_CONT, self.tid)
        while True:
            status = wait_stop(self.tid)
            if not os.WIFSTOPPED(status):
                raise PtraceError(f"target exited during call, status={status:#x}")
            sig = os.WSTOPSIG(status)
            stopped = get_regs(self.tid)
            if stopped.pc == TRAP_PC:
                return stopped
            if sig in (signal.SIGSTOP, signal.SIGCONT):
                # group-stop left over from PTRACE_ATTACH; thread hasn't run yet
                ptrace(PTRACE_CONT, self.tid)
                continue
            raise PtraceError(f"call stopped at {stopped.pc:#x} signal={sig}, expected trap {TRAP_PC:#x}")

    def call_ptr(self, rva: int, args: Optional[List[int]] = None,
                 instance: Optional[int] = None) -> int:
        return u64_from_regs(self.call(rva, args, instance=instance), 0)

    def call_u32(self, rva: int, args: Optional[List[int]] = None,
                 instance: Optional[int] = None) -> int:
        return u64_from_regs(self.call(rva, args, instance=instance), 0) & 0xFFFFFFFF

    def call_f32(self, rva: int, args: Optional[List[int]] = None,
                  instance: Optional[int] = None) -> float:
        """AAPCS64 returns float in s0 (v0.s[0])."""
        self.call(rva, args, instance=instance)
        fp = get_fp_regs(self.tid)
        return f32_from_v(fp, 0, 0)

    def call_vec3(self, rva: int, args: Optional[List[int]] = None,
                  instance: Optional[int] = None,
                  fp_args: Optional[List[Tuple[int, int, float]]] = None) -> Tuple[float, float, float]:
        """AAPCS64 returns Vector3 (3-float HFA) in v0.s[0:2], v1.s[0]."""
        self.call(rva, args, instance=instance, fp_args=fp_args)
        fp = get_fp_regs(self.tid)
        return (f32_from_v(fp, 0, 0), f32_from_v(fp, 0, 1), f32_from_v(fp, 1, 0))

    def call_quat(self, rva: int, args: Optional[List[int]] = None,
                  instance: Optional[int] = None) -> Tuple[float, float, float, float]:
        """AAPCS64 returns Quaternion (4-float HFA) in v0.s[0:2], v1.s[0:1]."""
        self.call(rva, args, instance=instance)
        fp = get_fp_regs(self.tid)
        return (f32_from_v(fp, 0, 0), f32_from_v(fp, 0, 1),
                f32_from_v(fp, 1, 0), f32_from_v(fp, 1, 1))

    def set_quat(self, rva: int, q: Tuple[float, float, float, float],
                 instance: Optional[int] = None) -> None:
        """Quaternion argument is a 4-float HFA passed in v0/v1."""
        if instance is None:
            raise PtraceError("Transform.set_rotation needs an instance")
        self.call(rva, instance=instance,
                  fp_args=[(0, 0, q[0]), (0, 1, q[1]), (1, 0, q[2]), (1, 1, q[3])])

    def set_quat_no_instance(self, rva: int, q: Tuple[float, float, float, float]) -> None:
        self.call(rva, fp_args=[(0, 0, q[0]), (0, 1, q[1]), (1, 0, q[2]), (1, 1, q[3])])

    def read_word(self, addr: int) -> int:
        """Read 8 bytes via PTRACE_PEEKTEXT (safe for -1 values)."""
        ctypes.set_errno(0)
        raw = libc.ptrace(PTRACE_PEEKTEXT, self.tid, ctypes.c_void_p(addr), ctypes.c_void_p(0))
        if raw == -1:
            err = ctypes.get_errno()
            if err != 0:
                raise PtraceError(f"PEEKTEXT @{addr:#x} errno={err} {os.strerror(err)}")
        return raw & 0xFFFFFFFFFFFFFFFF

    def write_word(self, addr: int, value: int) -> None:
        ptrace(PTRACE_POKETEXT, self.tid, addr, value & 0xFFFFFFFFFFFFFFFF)

    def patch_bytes(self, addr: int, data: bytes) -> None:
        """Patch arbitrary bytes at addr; must be attached. Writes 8-byte words."""
        if not self._attached:
            raise PtraceError("not attached")
        end = addr + len(data)
        cur = addr & ~7
        while cur < end:
            word = self.read_word(cur)
            word_bytes = bytearray(struct.pack("<Q", word))
            for i in range(8):
                a = cur + i
                if addr <= a < end:
                    word_bytes[i] = data[a - addr]
            self.write_word(cur, struct.unpack("<Q", bytes(word_bytes))[0])
            cur += 8


def remote_vec3(remote: Il2CppRemote, rva: int, instance: Optional[int] = None) -> Tuple[float, float, float]:
    return remote.call_vec3(rva, instance=instance)


def elf_load_bias(mem: Any, lib: str, rva: int) -> Optional[int]:
    """Resolve the real ELF load bias. Android maps libil2cpp.so several times;
    the first r--p mapping is not always the bias used by dump.cs RVAs."""
    maps = [m for m in mem.maps if lib.lower() in m["path"].lower()]
    if not maps:
        return None
    path = maps[0]["path"]
    with open(path, "rb") as fh:
        header = fh.read(0x1000)
        if header[:4] != b"\x7fELF":
            return None
        e_phoff = struct.unpack_from("<Q", header, 0x20)[0]
        e_phentsize = struct.unpack_from("<H", header, 0x36)[0]
        e_phnum = struct.unpack_from("<H", header, 0x38)[0]
        fh.seek(e_phoff)
        phdrs = fh.read(e_phentsize * e_phnum)

    segments = []
    for i in range(e_phnum):
        off = i * e_phentsize
        p_type, p_flags = struct.unpack_from("<II", phdrs, off)
        if p_type != 1:  # PT_LOAD
            continue
        p_offset, p_vaddr, _p_paddr, p_filesz, _p_memsz, _p_align = struct.unpack_from("<QQQQQQ", phdrs, off + 8)
        segments.append((p_offset, p_vaddr, p_filesz))

    candidates = set()
    zero_offset = set()
    for m in maps:
        foff = m.get("offset", 0)
        for p_offset, p_vaddr, p_filesz in segments:
            if p_offset <= foff < p_offset + p_filesz:
                bias = m["start"] - (p_vaddr + (foff - p_offset))
                candidates.add(bias)
                if foff == 0:
                    zero_offset.add(bias)

    def is_exec(b: int) -> bool:
        return any(m["start"] <= b + rva < m["end"] and "x" in m["perms"] for m in mem.maps)

    executable = [b for b in candidates if is_exec(b)]
    if not executable:
        return None
    preferred = [b for b in zero_offset if b in executable]
    if preferred:
        return max(preferred)
    return max(executable)


def read_list(mem: Any, list_ptr: int) -> List[int]:
    """IL2CPP List<T>: items @0x10, size @0x18, array data @0x20 (object header + bounds + length)."""
    if not list_ptr:
        return []
    items = mem.ptr(list_ptr + 0x10)
    size = mem.u32(list_ptr + 0x18)
    if not items or size > 4096:
        return []
    return [mem.ptr(items + 0x20 + i * 8) for i in range(size)]
