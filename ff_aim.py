#!/usr/bin/env python3
# language: Python 3, file: ff_aim.py, target: rooted Android ARM64 / Termux, Free Fire IL2CPP
# aimlock driver: ptrace calls GameFacade/Player/Transform accessors found in dump.cs,
# reads the live player list, solves yaw/pitch, writes the camera rotation back.

import argparse
import json
import math
import os
import subprocess
import sys
import time
from typing import Any, Dict, List, Optional, Tuple

from ff_aimlock import ProcessMemory, MemError, find_pid, angle_deg, look_rotation, slerp, quat_forward, Vec3, Quat
from il2cpp_remote import Il2CppRemote, PtraceError, read_list, elf_load_bias


def as_int(v: Any) -> int:
    return v if isinstance(v, int) else int(str(v), 0)


class AimlockRemote:
    def __init__(self, cfg: Dict[str, Any]):
        self.cfg = cfg
        self.rva = {k: as_int(v) for k, v in cfg["rva"].items()}
        self.fields = {k: as_int(v) for k, v in cfg["fields"].items()}
        self.settings = cfg.get("settings", {})
        self.pid = find_pid(cfg.get("process", ""))
        if not self.pid:
            raise MemError("game process not found — start the match first")
        self.mem = ProcessMemory(self.pid).open()
        probe = int(str(cfg["rva"]["GameFacade.CurrentLocalPlayer"]), 0)
        self.base = 0
        # the process may still be at the launcher/login screen: libil2cpp is
        # mapped only after the Unity runtime starts.
        for _ in range(120):
            self.base = elf_load_bias(self.mem, cfg.get("lib", "libil2cpp.so"), probe)
            if self.base:
                break
            self.mem.refresh_maps()
            time.sleep(0.5)
        if not self.base:
            raise MemError("libil2cpp.so not mapped")
        self.remote = Il2CppRemote(self.pid, self.base)
        self.list_ptr = 0
        self._no_recoil_patched = False
        self._hitbox_patched: set = set()

    def resolve(self) -> None:
        r = self.remote
        self.mem.refresh_maps()
        fresh_base = elf_load_bias(self.mem, self.cfg.get("lib", "libil2cpp.so"),
                                   self.rva["GameFacade.CurrentLocalPlayer"])
        if fresh_base and fresh_base != self.base:
            self.base, r.base = fresh_base, fresh_base
        self.local = r.call_ptr(self.rva["GameFacade.CurrentLocalPlayer"])
        self.match = r.call_ptr(self.rva["GameFacade.CurrentMatch"])
        if not self.local or not self.match:
            raise MemError("CurrentLocalPlayer/CurrentMatch returned null — not in a match?")
        cam_field = mem_ptr(self.mem, self.local + self.fields["player.main_camera_transform"])
        self.cam_tf = cam_field or r.call_ptr(
            self.rva["Player.GetRuntimeMainCameraTransform"], instance=self.local)
        if not self.cam_tf:
            raise MemError("camera transform not resolved")
        self.cam = 0
        cam_main_rva = self.rva.get("Camera.get_main")
        if cam_main_rva:
            self.cam = r.call_ptr(cam_main_rva)
        self.screen_w = 1080
        self.screen_h = 2400
        sw_rva = self.rva.get("Screen.get_width")
        sh_rva = self.rva.get("Screen.get_height")
        if sw_rva and sh_rva:
            try:
                sw = r.call_u32(sw_rva)
                sh = r.call_u32(sh_rva)
                if sw > 0 and sh > 0:
                    self.screen_w, self.screen_h = int(sw), int(sh)
            except (PtraceError, MemError):
                pass
        self.players: List[int] = []
        self._hitbox_patched.clear()
        self.refresh_players()
        self.apply_no_recoil()
        self.apply_hitbox_expand()

    def apply_no_recoil(self) -> None:
        if self._no_recoil_patched or not self.settings.get("no_recoil", False):
            return
        patches = []
        apply_rva = self.rva.get("Recoil.Apply")
        if apply_rva:
            patches.append((self.base + apply_rva, b"\xC0\x03\x5F\xD6", "Recoil.Apply"))
        delta_rva = self.rva.get("Recoil.GetDelta")
        if delta_rva:
            # fmov s0, wzr; fmov s1, wzr; ret
            patches.append((self.base + delta_rva, b"\xE0\x03\x27\x1E\xE1\x03\x27\x1E\xC0\x03\x5F\xD6", "Recoil.GetDelta"))
        if not patches:
            return
        for addr, data, name in patches:
            try:
                cur = self.mem.read(addr, len(data))
                if cur == data:
                    print(f"[*] {name} already patched @{addr:#x}", flush=True)
                    continue
                self.remote.patch_bytes(addr, data)
                print(f"[+] patched {name} @{addr:#x}", flush=True)
            except Exception as exc:
                print(f"[!] patch {name} failed @{addr:#x}: {exc}", flush=True)
        self._no_recoil_patched = True

    def apply_hitbox_expand(self) -> None:
        if not self.settings.get("hitbox_expand", False):
            return
        set_radius_rva = self.rva.get("CapsuleCollider.set_radius")
        set_height_rva = self.rva.get("CapsuleCollider.set_height")
        if not set_radius_rva and not set_height_rva:
            return
        radius = float(self.settings.get("hitbox_radius", 1.2))
        height = float(self.settings.get("hitbox_height", 2.0))
        col_fields = [self.fields.get("player.collider_a"), self.fields.get("player.collider_b")]
        col_fields = [f for f in col_fields if f is not None]
        if not col_fields:
            return
        patched = 0
        for p in self.players:
            if not p or p == self.local or p in self._hitbox_patched:
                continue
            if not self.mem.is_readable(p, 0x10):
                continue
            try:
                if self.remote.call_u32(self.rva["GameFacade.IsLocalTeammate"], [p]):
                    continue
            except (PtraceError, MemError):
                pass
            ok = False
            for off in col_fields:
                col = mem_ptr(self.mem, p + off)
                if not col or not self.mem.is_readable(col, 0x20):
                    continue
                try:
                    get_radius_rva = self.rva.get("CapsuleCollider.get_radius")
                    if get_radius_rva:
                        cur = self.remote.call_f32(get_radius_rva, instance=col)
                        if not math.isfinite(cur) or cur < 0.01 or cur > 10.0:
                            continue
                    if set_radius_rva:
                        self.remote.call(set_radius_rva, instance=col, fp_args=[(0, 0, radius)])
                    if set_height_rva:
                        self.remote.call(set_height_rva, instance=col, fp_args=[(0, 0, height)])
                    ok = True
                except (PtraceError, MemError) as exc:
                    print(f"[!] hitbox expand failed player={p:#x} col={col:#x}: {exc}", flush=True)
            if ok:
                self._hitbox_patched.add(p)
                patched += 1
        if patched:
            print(f"[+] hitbox expanded: {patched} players (r={radius} h={height})", flush=True)

    def refresh_players(self) -> None:
        self.mem.refresh_maps()
        candidates: List[Tuple[int, List[int]]] = []
        for key, off in self.fields.items():
            if not key.startswith("match.player_list"):
                continue
            lp = mem_ptr(self.mem, self.match + off)
            if not lp:
                continue
            players = read_list(self.mem, lp)
            if players:
                candidates.append((lp, players))
        if not candidates:
            self.list_ptr, self.players = 0, []
            return
        # merge unique players from all candidate lists (some builds split by team/state)
        merged: List[int] = []
        seen = set()
        for _, players in candidates:
            for p in players:
                if p and p not in seen:
                    seen.add(p)
                    merged.append(p)
        self.list_ptr = max(candidates, key=lambda item: len(item[1]))[0]
        self.players = merged

    def head_position(self, player: int) -> Optional[Vec3]:
        r = self.remote
        bone = self.settings.get("bone", "head").lower()
        if bone == "chest":
            return r.call_vec3(self.rva["Player.GetChestPos"], [1], instance=player)
        if not self.mem.is_readable(player, 0x10):
            return None

        tf = r.call_ptr(self.rva["Player.get_HeadBoneTransform"], instance=player)
        if not tf:
            tf = r.call_ptr(self.rva["Player.GetHeadTF"], instance=player)

        get_tf = self.rva.get("Component.get_transform")
        head_col = self.rva.get("Player.get_HeadCollider")
        if (not tf or not self.mem.is_readable(tf, 0x40)) and head_col and get_tf:
            col = r.call_ptr(head_col, instance=player)
            if col and self.mem.is_readable(col, 0x10):
                tf = r.call_ptr(get_tf, instance=col)

        if not tf or not self.mem.is_readable(tf, 0x40):
            return None
        return r.call_vec3(self.rva["Transform.get_position"], instance=tf)

    def pick(self, cam_pos: Vec3, cam_fwd: Vec3) -> Optional[Tuple[int, Vec3, float, float]]:
        r = self.remote
        best = None
        best_score = 1e18
        fov_check = bool(self.settings.get("fov_check", True))
        aim_fov = float(self.settings.get("aim_fov", self.settings.get("max_angle", 45.0)))
        max_dist = float(self.settings.get("max_distance", 300.0))
        for p in self.players:
            if not p or p == self.local:
                continue
            try:
                if not self.mem.is_readable(p, 0x10):
                    continue
                if r.call_u32(self.rva["GameFacade.IsLocalTeammate"], [p]):
                    continue
                is_dead_rva = self.rva.get("Player.get_IsDead")
                if is_dead_rva and r.call_u32(is_dead_rva, instance=p):
                    continue
                pos = self.head_position(p)
            except (PtraceError, MemError):
                continue
            if not pos or not all(math.isfinite(v) for v in pos):
                continue
            direction = (pos[0] - cam_pos[0], pos[1] - cam_pos[1], pos[2] - cam_pos[2])
            d = math.sqrt(sum(v * v for v in direction))
            if d < 0.1 or d > max_dist:
                continue
            angle = angle_deg(cam_fwd, direction)
            if fov_check and angle > aim_fov:
                continue
            score = angle + d * 0.01
            if score < best_score:
                best_score, best = score, (p, pos, angle, d)
        return best

    def export_esp(self, frame: int = 0) -> None:
        if not prop_bool("meow.esp", self.settings.get("esp_enabled", False)):
            return
        if frame % 2 != 0:
            return
        if not self.cam or not self.mem.is_readable(self.cam, 0x10):
            cam_main_rva = self.rva.get("Camera.get_main")
            if cam_main_rva:
                try:
                    self.cam = self.remote.call_ptr(cam_main_rva)
                except (PtraceError, MemError):
                    self.cam = 0
        if not self.cam:
            return
        w2s_rva = self.rva.get("Camera.WorldToScreenPoint")
        if not w2s_rva:
            return

        boxes = []
        lines = []
        sw, sh = float(self.screen_w), float(self.screen_h)
        draw_box = prop_bool("meow.box", True)
        for p in self.players:
            if not p or p == self.local:
                continue
            try:
                if not self.mem.is_readable(p, 0x10):
                    continue
                if self.settings.get("esp_teammate_skip", True):
                    if self.remote.call_u32(self.rva["GameFacade.IsLocalTeammate"], [p]):
                        continue
                is_dead_rva = self.rva.get("Player.get_IsDead")
                if is_dead_rva and self.remote.call_u32(is_dead_rva, instance=p):
                    continue
                pos = self.head_position(p)
            except (PtraceError, MemError):
                continue
            if not pos or not all(math.isfinite(v) for v in pos):
                continue

            try:
                top3 = self.remote.call_vec3(w2s_rva, instance=self.cam,
                                             fp_args=[(0, 0, pos[0]), (0, 1, pos[1] + 0.8), (1, 0, pos[2])])
                bot3 = self.remote.call_vec3(w2s_rva, instance=self.cam,
                                             fp_args=[(0, 0, pos[0]), (0, 1, pos[1] - 0.8), (1, 0, pos[2])])
            except (PtraceError, MemError):
                continue
            if top3[2] < 1.0 or bot3[2] < 1.0:
                continue

            top = (top3[0], sh - top3[1])
            bot = (bot3[0], sh - bot3[1])
            h = abs(bot[1] - top[1])
            w = h * 0.6
            if h < 2 or h > sh * 2:
                continue
            left = top[0] - w / 2.0
            right = top[0] + w / 2.0
            up = min(top[1], bot[1])
            down = max(top[1], bot[1])
            if draw_box:
                boxes.append({"l": round(left, 1), "t": round(up, 1), "r": round(right, 1), "b": round(down, 1)})
            lines.append({"x1": round(sw * 0.5, 1), "y1": 0.0, "x2": round(top[0], 1), "y2": round(up, 1)})

        payload = json.dumps({"color": "#FFFFFF", "boxes": boxes, "lines": lines}, separators=(",", ":"))
        path = self.settings.get("esp_file", "/data/local/tmp/meow_esp.json")
        tmp = path + ".tmp"
        try:
            with open(tmp, "w", encoding="utf-8") as fh:
                fh.write(payload)
            os.replace(tmp, path)
        except OSError:
            pass

    def run(self) -> None:
        r = self.remote
        smooth = float(self.settings.get("smooth", 0.35))
        delay = float(self.settings.get("sleep_ms", 30)) / 1000.0
        frame = 0
        print(f"[+] pid={self.pid} bias={self.base:#x}", flush=True)
        while True:
            try:
                # attach only for the duration of one batch; leaving the target
                # stopped across sleep() Starves its main loop and triggers ANR.
                r.attach()
                try:
                    if frame == 0 or frame % 60 == 0:
                        self.resolve()
                        print(f"[+] local={self.local:#x} match={self.match:#x} "
                              f"cam={self.cam_tf:#x} list={self.list_ptr:#x} "
                              f"players={len(self.players)}", flush=True)
                    else:
                        self.refresh_players()
                        self.apply_hitbox_expand()
                    cam_pos = r.call_vec3(self.rva["Transform.get_position"], instance=self.cam_tf)
                    cam_rot = r.call_quat(self.rva["Transform.get_rotation"], instance=self.cam_tf)
                    weapon_ok = True
                    if self.settings.get("sniper_only", False):
                        wt_rva = self.rva.get("Player.GetWeaponType")
                        if wt_rva:
                            wt = r.call_u32(wt_rva, instance=self.local)
                            weapon_ok = (wt == 1)  # HBIBDMMOOOK.ELNCBDCJNLC.Sniper
                            if not weapon_ok and frame % 60 == 0:
                                print(f"[*] sniper_only: weapon_type={wt}, aimlock idle", flush=True)
                    if weapon_ok:
                        best = self.pick(cam_pos, quat_forward(cam_rot))
                        if best:
                            _, pos, angle, dist = best
                            desired = look_rotation((pos[0] - cam_pos[0],
                                                     pos[1] - cam_pos[1],
                                                     pos[2] - cam_pos[2]))
                            r.set_quat(self.rva["Transform.set_rotation"],
                                       slerp(cam_rot, desired, smooth), instance=self.cam_tf)
                            if frame % 20 == 0:
                                print(f"[*] lock player={best[0]:#x} angle={angle:.1f} dist={dist:.1f}", flush=True)
                    self.export_esp(frame)
                finally:
                    r.detach()
                frame += 1
                time.sleep(delay)
            except KeyboardInterrupt:
                r.detach()
                print("\n[*] stopped", flush=True)
                break
            except (PtraceError, MemError) as exc:
                r.detach()
                print(f"[!] {exc}", flush=True)
                time.sleep(0.3)


def mem_ptr(mem: ProcessMemory, addr: int) -> int:
    if not mem.is_readable(addr, 8):
        return 0
    return mem.ptr(addr)


def prop_bool(name: str, default: bool = False) -> bool:
    try:
        value = subprocess.check_output(["getprop", name], text=True, timeout=1).strip()
    except Exception:
        return default
    if value in ("1", "true", "True"):
        return True
    if value in ("0", "false", "False"):
        return False
    return default


def main() -> None:
    ap = argparse.ArgumentParser(description="ptrace IL2CPP aimlock from dump.cs RVAs")
    ap.add_argument("--config", default=os.path.join(os.path.dirname(__file__), "ff_rvas.json"))
    ap.add_argument("--bone", choices=["head", "chest"], help="Override target bone (default: config)")
    ap.add_argument("--lock-head", action="store_true", help="Force head lock (alias for --bone head)")
    args = ap.parse_args()
    with open(args.config, "r", encoding="utf-8") as fh:
        cfg = json.load(fh)
    if args.lock_head or args.bone:
        cfg.setdefault("settings", {})["bone"] = "head" if args.lock_head else args.bone
    try:
        AimlockRemote(cfg).run()
    except (MemError, PtraceError) as exc:
        print(f"[-] {exc}")


if __name__ == "__main__":
    main()
