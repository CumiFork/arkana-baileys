# v2-mod (MLBB Auto Retribution scaffold)

Separate from `v1/` read-only overlay. This folder is for hook/inject-based mod work.

## Status

- Dump-derived offsets are filled from `/storage/emulated/0/walib/dump.cs` (BattleManager.Update, ShowPlayer.m_iSummonSkillId, ShowEntity HP/flags, monster list).
- Detection scan runs in `hooked_battle_update`: finds wild monster HP% <= threshold and logs target.
- **Auto-cast invoke is still TODO.** `tryCastRetribution` does not call the game cast method yet; it needs a verified SkillComponent/LogicFighter cast path (dump reference: `SkillComponent.CastSpellToEnemy` RVA `0x29F0788`).

## Features

- **BattleManager.Update hook** via Dobby, installed after `libil2cpp.so` is found.
- **Config file**: `/data/local/tmp/v2_autoretribution.cfg` (key=value, `#` comments). Reloaded periodically while in game.
- **Status file**: `/data/local/tmp/v2_autoretribution.status` for quick Termux/debug inspection.
- **Safety gates**: cooldown, HP threshold clamp, monster scan cap, optional required summon skill ID, optional wild-monster-only filter.
- **Cast gate**: `cast_enabled` defaults to `0`; real cast invoke remains unimplemented until verified on target build.

## Config keys

```ini
# /data/local/tmp/v2_autoretribution.cfg
enabled=1                  # 1/0
hp_percent=20              # 1..100
cooldown_ms=500            # >= 0
only_wild_monsters=1       # 1/0
debug_log=1                # 1/0
cast_enabled=0             # 1/0, keep 0 until cast path is implemented/verified
required_skill_id=0        # 0 = any summon skill
max_monster_scan=64        # 1..256
config_reload_sec=3        # 0 disables periodic reload
```

## Build (ndk-build)

```bash
cd v2-mod/src/main/jni
ndk-build NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=Android.mk NDK_APPLICATION_MK=Application.mk
```

## What must be filled

1. Verify offsets against the target MLBB build/version.
2. Implement `tryCastRetribution` with a verified in-game cast call.
3. Test hook/detection/cast on a controlled account/build.

## Safety/constraints

- This is not `v1` read-only architecture.
- Test only on accounts/builds you own/control.
- `cast_enabled` is off by default because cast invoke is not implemented.