#!/usr/bin/env python3
# language: Python 3, file: ff_aimlock.py, target: rooted Android / Termux, IL2CPP Unity game
# root aimlock: /proc/<pid>/mem camera + entity reads, quaternion aim solve, camera rotation write.
# offsets are build-specific; resolve them with dump_extract.py against your dump.cs.

import argparse
import ctypes
import json
import math
import os
import re
import struct
import sys
import time
from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional, Tuple

Vec3 = Tuple[float, float, float]
Quat = Tuple[float, float, float, float]


# ---------------------------------------------------------------------------
# memory
# ---------------------------------------------------------------------------

class MemError(Exception):
    pass


class ProcessMemory:
    def __init__(self, pid: int):
        self.pid = pid
        self._fd: Optional[int] = None
        self.maps = self._read_maps()

    def refresh_maps(self) -> None:
        # heap and il2cpp mappings change while a match loads; is_readable()
        # must not reject pointers from regions mapped after startup.
        self.maps = self._read_maps()

    def _read_maps(self) -> List[Dict[str, Any]]:
        out = []
        path = f"/proc/{self.pid}/maps"
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            for line in fh:
                parts = line.split()
                if len(parts) < 5:
                    continue
                rng, perms = parts[0], parts[1]
                try:
                    start_s, end_s = rng.split("-")
                    out.append({
                        "start": int(start_s, 16),
                        "end": int(end_s, 16),
                        "perms": perms,
                        "offset": int(parts[2], 16),
                        "path": parts[5] if len(parts) >= 6 else "",
                    })
                except ValueError:
                    continue
        return out

    def open(self) -> "ProcessMemory":
        if self._fd is None:
            try:
                self._fd = os.open(f"/proc/{self.pid}/mem", os.O_RDWR)
            except OSError as exc:
                raise MemError(f"open /proc/{self.pid}/mem: {exc} (run as root)") from exc
        return self

    def close(self) -> None:
        if self._fd is not None:
            os.close(self._fd)
            self._fd = None

    def module_base(self, needle: str) -> Optional[int]:
        for m in self.maps:
            if needle.lower() in m["path"].lower():
                return m["start"]
        return None

    def module_range(self, needle: str) -> Optional[Tuple[int, int]]:
        for m in self.maps:
            if needle.lower() in m["path"].lower():
                return m["start"], m["end"]
        return None

    def is_readable(self, addr: int, size: int = 4) -> bool:
        return any(m["start"] <= addr and addr + size <= m["end"] and "r" in m["perms"]
                   for m in self.maps)

    def read(self, addr: int, size: int) -> bytes:
        if self._fd is None:
            self.open()
        try:
            data = os.pread(self._fd, size, addr)
        except OSError as exc:
            raise MemError(f"pread @{addr:#x} +{size}: {exc}") from exc
        if len(data) != size:
            raise MemError(f"short read @{addr:#x}: {len(data)}/{size}")
        return data

    def write(self, addr: int, data: bytes) -> None:
        if self._fd is None:
            self.open()
        try:
            n = os.pwrite(self._fd, data, addr)
        except OSError as exc:
            raise MemError(f"pwrite @{addr:#x}: {exc}") from exc
        if n != len(data):
            raise MemError(f"short write @{addr:#x}: {n}/{len(data)}")

    def u32(self, addr: int) -> int:
        return struct.unpack("<I", self.read(addr, 4))[0]

    def u64(self, addr: int) -> int:
        return struct.unpack("<Q", self.read(addr, 8))[0]

    def ptr(self, addr: int, bits: int = 64) -> int:
        return self.u64(addr) if bits == 64 else self.u32(addr)

    def f32(self, addr: int) -> float:
        return struct.unpack("<f", self.read(addr, 4))[0]

    def v3(self, addr: int) -> Vec3:
        return struct.unpack("<fff", self.read(addr, 12))

    def quat(self, addr: int) -> Quat:
        return struct.unpack("<ffff", self.read(addr, 16))

    def write_quat(self, addr: int, q: Quat) -> None:
        self.write(addr, struct.pack("<ffff", *q))

    def write_v3(self, addr: int, v: Vec3) -> None:
        self.write(addr, struct.pack("<fff", *v))

    def chain(self, base: int, offsets: List[int], bits: int = 64) -> int:
        addr = base
        for off in offsets:
            addr = self.ptr(addr + off, bits)
            if not addr:
                return 0
        return addr

    def scan(self, start: int, end: int, pattern: bytes, mask: bytes, step: int = 4) -> List[int]:
        """Byte-pattern scan inside a mapped module (mask: 'x' exact, '?' wildcard)."""
        hits: List[int] = []
        n = len(pattern)
        chunk = max(1 << 20, n * 1024)
        addr = start
        prev = b""
        while addr < end:
            size = min(chunk, end - addr)
            data = prev + self.read(addr, size)
            base = addr - len(prev)
            limit = len(data) - n
            for i in range(0, limit + 1, step):
                ok = True
                for j in range(n):
                    if mask[j] == "x" and data[i + j] != pattern[j]:
                        ok = False
                        break
                if ok:
                    hits.append(base + i)
            prev = data[-(n - 1):] if n > 1 else b""
            addr += size
        return hits


def find_pid(process: str) -> Optional[int]:
    """Match /proc/<pid>/cmdline against package name substring."""
    for entry in os.listdir("/proc"):
        if not entry.isdigit():
            continue
        try:
            with open(f"/proc/{entry}/cmdline", "rb") as fh:
                cmd = fh.read().replace(b"\x00", b" ").decode("utf-8", "replace")
        except OSError:
            continue
        if process in cmd:
            return int(entry)
    return None


# ---------------------------------------------------------------------------
# quaternion / vector math — Unity convention: +Z forward, +Y up, quaternion (x,y,z,w)
# ---------------------------------------------------------------------------

def dot(a: Vec3, b: Vec3) -> float:
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a: Vec3, b: Vec3) -> Vec3:
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def norm_v(v: Vec3) -> Vec3:
    m = math.sqrt(dot(v, v))
    return (v[0] / m, v[1] / m, v[2] / m) if m > 1e-9 else (0.0, 0.0, 1.0)


def mag(v: Vec3) -> float:
    return math.sqrt(dot(v, v))


def quat_forward(q: Quat) -> Vec3:
    x, y, z, w = q
    return norm_v((2 * (x * z + w * y), 2 * (y * z - w * x), 1 - 2 * (x * x + y * y)))


def norm_q(q: Quat) -> Quat:
    x, y, z, w = q
    m = math.sqrt(x * x + y * y + z * z + w * w)
    return (x / m, y / m, z / m, w / m) if m > 1e-9 else (0.0, 0.0, 0.0, 1.0)


def look_rotation(forward: Vec3, up: Vec3 = (0.0, 1.0, 0.0)) -> Quat:
    f = norm_v(forward)
    right = cross(up, f)
    if mag(right) < 1e-6:
        right = (1.0, 0.0, 0.0)
    r = norm_v(right)
    u = norm_v(cross(f, r))
    m00, m10, m20 = r
    m01, m11, m21 = u
    m02, m12, m22 = f
    tr = m00 + m11 + m22
    if tr > 0.0:
        s = math.sqrt(tr + 1.0) * 2
        w = 0.25 * s
        x = (m21 - m12) / s
        y = (m02 - m20) / s
        z = (m10 - m01) / s
    elif m00 > m11 and m00 > m22:
        s = math.sqrt(1.0 + m00 - m11 - m22) * 2
        w = (m21 - m12) / s
        x = 0.25 * s
        y = (m01 + m10) / s
        z = (m02 + m20) / s
    elif m11 > m22:
        s = math.sqrt(1.0 + m11 - m00 - m22) * 2
        w = (m02 - m20) / s
        x = (m01 + m10) / s
        y = 0.25 * s
        z = (m12 + m21) / s
    else:
        s = math.sqrt(1.0 + m22 - m00 - m11) * 2
        w = (m10 - m01) / s
        x = (m02 + m20) / s
        y = (m12 + m21) / s
        z = 0.25 * s
    return norm_q((x, y, z, w))


def slerp(a: Quat, b: Quat, t: float) -> Quat:
    t = max(0.0, min(1.0, t))
    x1, y1, z1, w1 = a
    x2, y2, z2, w2 = b
    d = x1 * x2 + y1 * y2 + z1 * z2 + w1 * w2
    if d < 0.0:
        x2, y2, z2, w2, d = -x2, -y2, -z2, -w2, -d
    if d > 0.9995:
        return norm_q((x1 + (x2 - x1) * t, y1 + (y2 - y1) * t,
                       z1 + (z2 - z1) * t, w1 + (w2 - w1) * t))
    theta = math.acos(max(-1.0, min(1.0, d)))
    s = math.sin(theta)
    k1 = math.sin((1.0 - t) * theta) / s
    k2 = math.sin(t * theta) / s
    return norm_q((x1 * k1 + x2 * k2, y1 * k1 + y2 * k2,
                   z1 * k1 + z2 * k2, w1 * k1 + w2 * k2))


def angle_deg(a: Vec3, b: Vec3) -> float:
    c = max(-1.0, min(1.0, dot(norm_v(a), norm_v(b))))
    return math.degrees(math.acos(c))


# ---------------------------------------------------------------------------
# config-driven pointer resolution
# ---------------------------------------------------------------------------

def as_int(value: Any) -> int:
    if isinstance(value, int):
        return value
    return int(str(value), 0)


def resolve_address(spec: Dict[str, Any], mem: ProcessMemory, base: int, bits: int,
                    lib: str = "libil2cpp.so") -> int:
    kind = spec.get("type", "static_rva").lower()
    if kind == "absolute":
        addr = as_int(spec["address"])
    elif kind == "static_rva":
        addr = base + as_int(spec["rva"])
    elif kind == "scan":
        rng = mem.module_range(lib)
        if not rng:
            raise MemError(f"module {lib} not mapped for scan")
        pat = bytes.fromhex(spec["pattern"].replace(" ", ""))
        mask = spec.get("mask", "x" * len(pat))
        hits = mem.scan(rng[0], rng[1], pat, mask.encode(), step=1)
        if not hits:
            raise MemError(f"scan found no match for {spec['pattern']}")
        addr = hits[0] + as_int(spec.get("hit_adjust", 0))
    else:
        raise MemError(f"unknown pointer type: {kind}")
    offs = [as_int(x) for x in spec.get("offsets", [])]
    if offs:
        addr = mem.chain(addr, offs, bits)
    addr += as_int(spec.get("final_add", 0))
    return addr


@dataclass
class AimState:
    target_addr: int = 0
    target_pos: Vec3 = (0.0, 0.0, 0.0)
    angle: float = 0.0
    distance: float = 0.0
    frames: int = 0
    hits: int = 0


class Aimlock:
    def __init__(self, cfg: Dict[str, Any], mem: ProcessMemory):
        self.cfg = cfg
        self.mem = mem
        self.bits = int(cfg.get("bits", 64))
        self.lib = str(cfg.get("lib", "libil2cpp.so"))
        self.base = mem.module_base(self.lib)
        if not self.base:
            raise MemError(f"module {self.lib} not found in /proc/{mem.pid}/maps")
        self.cam = cfg["camera"]
        self.ent = cfg["entities"]
        self.tgt = cfg["target"]
        self.settings = cfg.get("settings", {})
        self.state = AimState()
        self._cam_rot_addr = 0
        self._cam_pos_addr = 0
        self._list_addr = 0
        self._resolved = False

    def resolve(self) -> None:
        self._cam_rot_addr = resolve_address(self.cam["rotation"], self.mem, self.base, self.bits, self.lib)
        self._cam_pos_addr = resolve_address(self.cam["position"], self.mem, self.base, self.bits, self.lib)
        self._list_addr = resolve_address(self.ent["list"], self.mem, self.base, self.bits, self.lib)
        if not all((self._cam_rot_addr, self._cam_pos_addr, self._list_addr)):
            raise MemError("pointer resolution returned null — check offsets for this game build")
        self._resolved = True

    def _entity_ptrs(self) -> List[int]:
        spec = self.ent
        count_addr = resolve_address(spec["count"], self.mem, self.base, self.bits, self.lib) if "count" in spec else 0
        count = self.mem.u32(count_addr) if count_addr else int(spec.get("fixed_count", 0))
        count = min(max(0, count), int(spec.get("max_count", 256)))
        step = as_int(spec.get("entry_size", 8))
        data_addr = self._list_addr + as_int(spec.get("data_offset", 0))
        ptrs = []
        for i in range(count):
            p = self.mem.ptr(data_addr + i * step, self.bits)
            if p:
                ptrs.append(p)
        return ptrs

    def _target_from_ptr(self, p: int, cam_pos: Vec3,
                         my_team: Optional[int]) -> Optional[Tuple[int, Vec3, float, float]]:
        pos_off = as_int(self.tgt["position_offset"])
        bone = self.settings.get("bone", "head").lower()
        aim_off = as_int(self.tgt.get("head_offset", pos_off)) if bone == "head" else pos_off
        aim_pos = self.mem.v3(p + aim_off)
        if not all(math.isfinite(v) for v in aim_pos):
            return None
        team_off = self.tgt.get("team_offset")
        if team_off is not None and my_team is not None:
            if self.mem.u32(p + as_int(team_off)) == my_team:
                return None
        alive_off = self.tgt.get("alive_offset")
        if alive_off is not None and self.mem.read(p + as_int(alive_off), 1)[0] == 0:
            return None
        d = mag((aim_pos[0] - cam_pos[0], aim_pos[1] - cam_pos[1], aim_pos[2] - cam_pos[2]))
        if d < 0.1 or d > float(self.settings.get("max_distance", 300.0)):
            return None
        return p, aim_pos, d, d

    def _local_team(self) -> Optional[int]:
        local = self.cfg.get("local_player")
        off = self.tgt.get("local_team_offset")
        if not local or off is None:
            return None
        ptr = resolve_address(local, self.mem, self.base, self.bits, self.lib)
        return self.mem.u32(ptr + as_int(off)) if ptr else None

    def pick_target(self, cam_pos: Vec3, cam_fwd: Vec3) -> Optional[Tuple[int, Vec3, float, float]]:
        max_angle = float(self.settings.get("max_angle", 45.0))
        my_team = self._local_team()
        best = None
        best_score = 1e18
        for p in self._entity_ptrs():
            got = self._target_from_ptr(p, cam_pos, my_team)
            if not got:
                continue
            _, pos, d, _ = got
            direction = (pos[0] - cam_pos[0], pos[1] - cam_pos[1], pos[2] - cam_pos[2])
            a = angle_deg(cam_fwd, direction)
            if a > max_angle:
                continue
            score = a + d * 0.01
            if score < best_score:
                best_score = score
                best = got
        if best:
            direction = (best[1][0] - cam_pos[0], best[1][1] - cam_pos[1], best[1][2] - cam_pos[2])
            best = (best[0], best[1], angle_deg(cam_fwd, direction), best[3])
        return best

    def run(self) -> None:
        if not self._resolved:
            self.resolve()
        smooth = float(self.settings.get("smooth", 0.35))
        delay = float(self.settings.get("sleep_ms", 8)) / 1000.0
        print(f"[+] base {self.base:#x} cam_rot {self._cam_rot_addr:#x} "
              f"cam_pos {self._cam_pos_addr:#x} list {self._list_addr:#x}", flush=True)
        while True:
            try:
                cam_pos = self.mem.v3(self._cam_pos_addr)
                cam_rot = self.mem.quat(self._cam_rot_addr)
                fwd = quat_forward(cam_rot)
                best = self.pick_target(cam_pos, fwd)
                if best:
                    p, pos, angle, dist = best
                    desired = look_rotation((pos[0] - cam_pos[0],
                                             pos[1] - cam_pos[1],
                                             pos[2] - cam_pos[2]))
                    out = slerp(cam_rot, desired, smooth)
                    self.mem.write_quat(self._cam_rot_addr, out)
                    self.state.target_addr = p
                    self.state.target_pos = pos
                    self.state.angle = angle
                    self.state.distance = dist
                    self.state.hits += 1
                else:
                    self.state.target_addr = 0
                self.state.frames += 1
                time.sleep(delay)
            except KeyboardInterrupt:
                print("\n[*] stopped", flush=True)
                break
            except MemError as exc:
                print(f"[!] {exc}", flush=True)
                time.sleep(0.25)


# ---------------------------------------------------------------------------
# cli
# ---------------------------------------------------------------------------

def load_config(path: str) -> Dict[str, Any]:
    with open(path, "r", encoding="utf-8") as fh:
        return json.load(fh)


def cmd_info(args: argparse.Namespace) -> None:
    pid = find_pid(args.process) if args.process else None
    if pid:
        mem = ProcessMemory(pid)
        print(f"[+] pid={pid}")
        for m in mem.maps:
            if any(k in m["path"] for k in ("il2cpp", "unity", "libunity", args.lib)):
                print(f"    {m['start']:#x}-{m['end']:#x} {m['perms']} {m['path']}")
    else:
        print("[-] process not found; is the game running?")


def cmd_run(args: argparse.Namespace) -> None:
    cfg = load_config(args.config)
    pid = args.pid or find_pid(cfg.get("process", ""))
    if not pid:
        print("[-] game process not found")
        return
    mem = ProcessMemory(pid).open()
    Aimlock(cfg, mem).run()


def cmd_scan(args: argparse.Namespace) -> None:
    pid = find_pid(args.process)
    if not pid:
        print("[-] process not found")
        return
    mem = ProcessMemory(pid).open()
    rng = mem.module_range(args.lib)
    if not rng:
        print(f"[-] {args.lib} not mapped")
        return
    pat = bytes.fromhex(args.pattern.replace(" ", ""))
    mask = args.mask or ("x" * len(pat))
    hits = mem.scan(rng[0], rng[1], pat, mask.encode(), step=1)
    for h in hits:
        print(f"{h:#x} base+{h - rng[0]:#x}")


def main() -> None:
    ap = argparse.ArgumentParser(description="root aimlock for IL2CPP Unity games")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p_info = sub.add_parser("info")
    p_info.add_argument("--process", default="")
    p_info.add_argument("--lib", default="libil2cpp.so")
    p_info.set_defaults(func=cmd_info)

    p_run = sub.add_parser("run")
    p_run.add_argument("--config", required=True)
    p_run.add_argument("--pid", type=int, default=0)
    p_run.set_defaults(func=cmd_run)

    p_scan = sub.add_parser("scan")
    p_scan.add_argument("--process", required=True)
    p_scan.add_argument("--lib", default="libil2cpp.so")
    p_scan.add_argument("--pattern", required=True, help="hex bytes, e.g. '00 00 80 3f'")
    p_scan.add_argument("--mask", default="", help="x=exact ?=wildcard")
    p_scan.set_defaults(func=cmd_scan)

    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
