# Root / bootloader detection findings

Target: `com.dts.freefireth`, dump source `/storage/emulated/0/freefire_dump/dump.cs`.

## What dump.cs actually contains

There is no readable `isRooted`, `RootBeer`, `BootloaderUnlock`, or similar symbol in `dump.cs`. The anti-cheat layer is split:

1. Obfuscated managed wrappers in `Assembly-CSharp.dll`
   - `AnoSDKNamespace` enums and classes around dump lines 35760-35960
   - `ffantihack.DKPDEMLPNOE`, dump line 35981, methods at RVA `0x3ECC340`-`0x3ECE464`
   - `ffantihack.MHJKABKLFEB`, dump line 36060, methods at RVA `0x3ECEDE8`-`0x3ED30E8`
   - These expose config setters such as `DKPDEMLPNOE.BKIHNKHJHAM(string, bool)` at `0x3ECC484`, but the dump has no plaintext key names.
2. Native ACE / AnoSDK libraries
   - `libanogs.so`, 5.3 MB, main detection and reporting engine
   - `libanort.so`, small companion library
   - `libGGP.so`, protocol library

The real root and bootloader decisions live in `libanogs.so`, not in `dump.cs`.

## Native strings recovered from libanogs.so

Strings are stored with a rolling XOR. Decrypted messages:

- `0x94160`, key `0xEF`: `Your device has been rooted, which poses a security risk. Please resume the game after returning to a non-Root environment.`
- `0x92759`, key `0xAA`: `You are using an unlocked device that is compromised or modified. Please restore to the locked state or change the device to continue.`

Other related strings: `mt2_no_root` at `0x8EB8B`, `name=%s|rate=%.2f|is_root=%d` at `0x942DD`, `inline_hook_opcode_dismatch`, `ptrace_stop`, `/proc/%d/wchan`, `/proc/%d/task/%d/wchan`, `com.ace.gamesafe4`.

## Patched RVAs in libanogs.so

| Name | RVA | Effect |
|---|---|---|
| `root_decision` | `0x250FF8` | combined root decision returns 0 |
| `unlock_decision` | `0x2B3DA0` | bootloader unlock decision returns 0 |
| `root_report` | `0x284420` | root warning report builder returns early |
| `unlock_report` | `0x2B3E08` | unlock warning report builder returns early |
| `mt2_no_root_branch` | `0x24A10C` | config flag no longer sets detection byte |
| `root_telemetry` | `0x25097C` | `is_root` telemetry always 0 |

Both report builders send event code `0x3F2` with the decrypted warning text. Returning early removes the in-game warning and report path.

## Usage

`projek/src/main/jni/main.cpp` now applies these patches from inside the game when `libMEOW.so` is injected. A watcher thread waits for `libanogs.so`, applies all six patches, and rechecks them every second. The ImGui menu shows `Active patches: n/6` and has a `Reapply Now` button.

Rebuild `libMEOW.so` after this source change. The existing `.so` files are stale.

The standalone patcher remains useful for verification:

```sh
su -c './run_antidetect.sh --check'
su -c './run_antidetect.sh'
```

Restore with `su -c './run_antidetect.sh --restore'`. Select individual patches with `--only root_report unlock_report`.

## Caveats

- Patches are in process memory only. After a game restart, reinject `libMEOW.so` or rerun `run_antidetect.sh`.
- Game updates change all RVAs. Re-derive them from the new `libanogs.so` before applying.
- Server-side reputation, ban waves, or behavior analysis are not affected by local patches.
- The aimlock still needs live verification of camera rotation and target lock.
