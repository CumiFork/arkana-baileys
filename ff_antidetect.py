#!/usr/bin/env python3
# language: Python 3, file: ff_antidetect.py, target: rooted Android / Termux
# Disables the native anti-cheat warning reports for root and bootloader unlock.
# RVAs are from this build of libanogs.so. Re-derive them after a game update.

import argparse
import os
import struct
import sys
from typing import Dict, Optional

from ff_aimlock import ProcessMemory, MemError, find_pid
from il2cpp_remote import elf_load_bias

PACKAGE = "com.dts.freefireth"
LIB = "libanogs.so"

RET = bytes.fromhex("c05fd6031f2003d5")  # ret; nop
NOP = bytes.fromhex("1f2003d5")

# name -> (rva, original bytes expected, patch bytes, description)
PATCHES: Dict[str, tuple] = {
    "root_decision": (
        0x250FF8,
        bytes.fromhex("fd7bbea9f44f01a9"),
        bytes.fromhex("1f008052c0035fd6"),  # mov w0, #0; ret
        "combined root decision function always returns not-root",
    ),
    "unlock_decision": (
        0x2B3DA0,
        bytes.fromhex("010200b4fd7bbea9"),
        bytes.fromhex("1f008052c0035fd6"),  # mov w0, #0; ret
        "bootloader unlock decision function always returns not-unlocked",
    ),
    "root_report": (
        0x284420,
        bytes.fromhex("fd7bbea9fc0b00f9"),
        RET,
        "root warning report builder: returns before building/sending code 0x3f2 message",
    ),
    "unlock_report": (
        0x2B3E08,
        bytes.fromhex("fd7bbea9fc0b00f9"),
        RET,
        "bootloader unlock warning report builder: returns before building/sending code 0x3f2 message",
    ),
    "mt2_no_root_branch": (
        0x24A10C,
        bytes.fromhex("88010037"),
        NOP,
        "mt2_no_root config flag no longer routes into the detection flag path",
    ),
    "root_telemetry": (
        0x25097C,
        bytes.fromhex("ff8306d1fd7b17a9"),
        bytes.fromhex("1f008052c0035fd6"),  # mov w0, #0; ret
        "is_root telemetry formatter always reports 0",
    ),
    "debugger_check_branch": (
        0x25EAD8,
        bytes.fromhex("a0020054"),  # b.eq 0x25eb2c
        NOP,
        "skip the ptrace and debugger check block so an external tracer is never evaluated",
    ),
    "debugger_status_getter": (
        0x25E9C8,
        bytes.fromhex("007440b9c0035fd6"),
        bytes.fromhex("1f008052c0035fd6"),  # mov w0, #0; ret
        "native debugger status getter always returns clean",
    ),
    "debugger_flag_getter": (
        0x25E9D0,
        bytes.fromhex("00e04139c0035fd6"),
        bytes.fromhex("1f008052c0035fd6"),  # mov w0, #0; ret
        "native debugger flag getter always returns clean",
    ),
    "native_report_sink": (
        0x2594AC,
        bytes.fromhex("fd7bbca9fc0b00f9"),
        RET,
        "shared native detection report builder returns before sending any event packet",
    ),
    "warning_report_send": (
        0x2570B8,
        bytes.fromhex("000440f9600100b4"),
        bytes.fromhex("1f008052c0035fd6"),  # mov w0, #0; ret
        "shared warning packet sender returns success without sending any detection report",
    ),
    "gpu_profiler_report": (
        0x256B2C,
        bytes.fromhex("fd7bbea9fc0b00f9"),
        RET,
        "GPU profiler warning builder returns before the event 0x3f2 message is built",
    ),
    "autoclick_report": (
        0x27A8DC,
        bytes.fromhex("fd7bbea9fc4f01a9"),
        RET,
        "AutoClicker warning builder returns before the event 0x3f3 message is built",
    ),
    "root_alert_flag": (
        0x2D1700,
        bytes.fromhex("ff0302d1fd7b06a9"),
        RET,
        "root alert setter returns before the native root alert flag is stored",
    ),
    "emulator_alert_flag": (
        0x28ADC4,
        bytes.fromhex("ff0302d1fd7b06a9"),
        RET,
        "emulator alert setter returns before the native emulator alert flag is stored",
    ),
}


def resolve_bias(mem: ProcessMemory) -> Optional[int]:
    mem.refresh_maps()
    if not any(LIB.lower() in m["path"].lower() for m in mem.maps):
        return None
    return elf_load_bias(mem, LIB, 0x284420)


def read_state(mem: ProcessMemory, bias: int) -> Dict[str, str]:
    state = {}
    for name, (rva, expected, patched, _desc) in PATCHES.items():
        addr = bias + rva
        if not mem.is_readable(addr, max(len(expected), len(patched))):
            state[name] = "UNMAPPED"
            continue
        cur = mem.read(addr, max(len(expected), len(patched)))
        if cur.startswith(expected):
            state[name] = "ORIGINAL"
        elif cur.startswith(patched):
            state[name] = "PATCHED"
        else:
            state[name] = "UNKNOWN " + cur.hex(" ")
    return state


def apply(mem: ProcessMemory, bias: int, names: list) -> None:
    for name in names:
        rva, expected, patched, desc = PATCHES[name]
        addr = bias + rva
        cur = mem.read(addr, max(len(expected), len(patched)))
        if cur.startswith(patched):
            print(f"[=] {name}: already patched")
            continue
        if not cur.startswith(expected):
            print(f"[!] {name}: unexpected bytes {cur.hex(' ')}, skipped")
            continue
        try:
            mem.write(addr, patched)
        except MemError as exc:
            print(f"[!] {name}: write failed: {exc}")
            continue
        verify = mem.read(addr, len(patched))
        if verify != patched:
            print(f"[!] {name}: verify failed {verify.hex(' ')}")
            continue
        print(f"[+] {name}: patched @ {LIB}+{rva:#x} ({desc})")


def restore(mem: ProcessMemory, bias: int, names: list) -> None:
    for name in names:
        rva, expected, patched, _desc = PATCHES[name]
        addr = bias + rva
        cur = mem.read(addr, max(len(expected), len(patched)))
        if cur.startswith(expected):
            print(f"[=] {name}: already original")
            continue
        if not cur.startswith(patched):
            print(f"[!] {name}: unexpected bytes {cur.hex(' ')}, skipped")
            continue
        try:
            mem.write(addr, expected)
        except MemError as exc:
            print(f"[!] {name}: write failed: {exc}")
            continue
        print(f"[+] {name}: restored @ {LIB}+{rva:#x}")


def main() -> int:
    ap = argparse.ArgumentParser(description="Disable native root / bootloader-unlock warning reports")
    ap.add_argument("--pid", type=int, help="game pid (auto detected if omitted)")
    ap.add_argument("--package", default=PACKAGE)
    ap.add_argument("--restore", action="store_true", help="restore original bytes")
    ap.add_argument("--check", action="store_true", help="only print current state")
    ap.add_argument("--only", nargs="*", choices=list(PATCHES), help="apply/restore selected patches")
    args = ap.parse_args()

    pid = args.pid or find_pid(args.package)
    if not pid:
        print("[-] game not running")
        return 1

    mem = ProcessMemory(pid)
    bias = resolve_bias(mem)
    if bias is None:
        print(f"[-] {LIB} not mapped in pid {pid}; enter the game first")
        return 1

    print(f"[*] pid={pid} {LIB} bias={bias:#x}")
    state = read_state(mem, bias)
    for name, value in state.items():
        print(f"    {name:22s} {value}")

    if args.check:
        return 0

    names = args.only or list(PATCHES)
    if args.restore:
        restore(mem, bias, names)
    else:
        apply(mem, bias, names)
    mem.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
