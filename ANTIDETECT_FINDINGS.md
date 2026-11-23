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

## Native debugger and ptrace detection

`libanogs.so` has a dedicated debugger checker around `0x25E9D8`. It branches at `0x25EAD8` into a block that calls `ptrace` with `PTRACE_ATTACH` (`0x10`), `PTRACE_DETACH` (`0x11`), `PTRACE_GETREGSET` (`0x4204`), and `PTRACE_SETREGSET` (`0x4205`). The block also checks the stopped instruction and the `0x4111AD` pattern before restoring registers.

The checker records its result in an object field and exposes it through two small getters:

- `0x25E9C8`, `ldr w0, [x0, #0x74]`, returns the numeric debugger status.
- `0x25E9D0`, `ldrb w0, [x0, #0x78]`, returns the debugger flag.

Both getters are called from JNI wrappers around `0x2BB6D0`, so managed anti-cheat code can read the result. All native detection reports converge on the shared builder at `0x2594AC`, which serializes an event ID and sends it through the in-game report channel.

## Patched RVAs in libanogs.so

| Name | RVA | Effect |
|---|---|---|
| `root_decision` | `0x250FF8` | combined root decision returns 0 |
| `unlock_decision` | `0x2B3DA0` | bootloader unlock decision returns 0 |
| `root_report` | `0x284420` | root warning report builder returns early |
| `unlock_report` | `0x2B3E08` | unlock warning report builder returns early |
| `mt2_no_root_branch` | `0x24A10C` | config flag no longer sets detection byte |
| `root_telemetry` | `0x25097C` | `is_root` telemetry always 0 |
| `debugger_check_branch` | `0x25EAD8` | debugger checker never enters the ptrace block |
| `debugger_status_getter` | `0x25E9C8` | native debugger status always 0 |
| `debugger_flag_getter` | `0x25E9D0` | native debugger flag always 0 |
| `native_report_sink` | `0x2594AC` | shared native detection report builder returns early |

Both report builders send event code `0x3F2` with the decrypted warning text. Returning early removes the in-game warning and report path. The report sink patch covers every other native detection event as well.

## Usage

`projek/src/main/jni/main.cpp` now applies these patches from inside the game when `libMEOW.so` is injected. A watcher thread waits for `libanogs.so`, applies all ten patches, and rechecks them every second. The ImGui menu shows `Active patches: n/10` and has a `Reapply Now` button.

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
