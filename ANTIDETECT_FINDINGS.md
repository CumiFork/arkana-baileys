# Native anti-cheat audit: libanogs and dump.cs

Target: `com.dts.freefireth`, package dump at `/storage/emulated/0/freefire_dump/dump.cs`.

## Scope

This audit covers local detection and report paths that can be observed in the installed build. It does not cover server-side reputation, ban waves, or behavior analysis. Every RVA below was derived from the current `libanogs.so` inside `split_config.arm64_v8a.apk` and checked against the local ELF bytes before patching.

## Managed surface in dump.cs

`dump.cs` has no readable `isRooted`, `RootBeer`, `BootloaderUnlock`, or `AntiCheat` decision method. The managed layer wraps a native SDK:

- `AnoSDKNamespace` enums and classes, around dump lines 35760 to 35960.
- `ffantihack.DKPDEMLPNOE`, dump line 35981, methods at `0x3ECC340` to `0x3ECE464`.
- `ffantihack.MHJKABKLFEB`, dump line 36060, methods at `0x3ECEDE8` to `0x3ED30E8`.
- Config setters such as `DKPDEMLPNOE.BKIHNKHJHAM(string, bool)` at `0x3ECC484` use obfuscated keys.

The decisions are native. The managed wrappers consume native status fields, so patching the native decision and report paths also removes the values those wrappers read.

## Decrypted native strings

Most detection strings are stored with a rolling XOR. The first byte gives the starting key and the key increments by one for each following byte. Decrypted strings relevant to detection:

| RVA | Decrypted string |
|---|---|
| `0x8D099` | `com.google.android.gapid.arm64-v8a` |
| `0x8D0BD` | `org.renderdoc.renderdoccmd.arm64` |
| `0x8D42F` | `libfake-linker-26.so` |
| `0x8D4A5` | `emu_alert:%s` |
| `0x8D799` | `frida-agent-64.so` |
| `0x8DC45` | `JDWP Command Re` |
| `0x8E079` | `root_alert:%s` |
| `0x8E3DA` | `AutoClicker found on your phone. To continue, drag the scroll bar to the number %d.` |
| `0x8E7A8` | `de.robv.android.xposed.installer` |
| `0x8EB97` | `/proc/%d/task/%d/wchan` |
| `0x8FB17` | `/proc/%u/status` |
| `0x8FB41` | `app_process64_xposed` |
| `0x8FF0B` | `com.powervr.carbon.payload` |
| `0x8FF6B` | `/sdcard/VMOSfiletransferstation` |
| `0x90670` | `libfake-linker-21.so` |
| `0x91B28` | `com.ano.gshell.GP6Service` |
| `0x921EA` | `frida-agent-32.so` |
| `0x92759` | unlocked-device warning message |
| `0x93030` | GPU profiler warning message |
| `0x93427` | `libxposed_art.so` |
| `0x940D5` | `XposedBridge.jar` |
| `0x9414E` | `com.ace.gamesafe4` |
| `0x94160` | rooted-device warning message |
| `0x94FE0` | `/proc/%d/wchan` |

## Verified detection paths

| Category | Evidence | Native code site | Disposition |
|---|---|---|---|
| Root decision | `mt2_no_root`, `name=%s|rate=%.2f|is_root=%d` | `0x250FF8`, `0x25097C`, `0x24A10C`, `0x2D1700` | decision, telemetry, and alert flag patched |
| Bootloader unlock | decrypted warning at `0x92759` | `0x2B3DA0`, `0x2B3E08` | decision and report patched |
| Debugger and ptrace | `debugger=%s`, `PTRACE_ATTACH`, `PTRACE_GETREGSET` | `0x25E9C8`, `0x25E9D0`, `0x25EAD8` | getters and checker branch patched |
| Shared report builder | event code `0x3F2` | `0x2594AC` | patched |
| Warning packet sender | vtable slot at `0x506DB8`, target `0x2570B8` | `0x2570B8` | patched, returns success without sending |
| GPU profiler | GPU profiler warning string | `0x256B2C`, caller `0x256AC4` | report builder patched |
| AutoClicker | AutoClicker warning string, event `0x3F3` | `0x27A8DC` | report builder patched |
| Emulator and virtual device | `emu_alert:%s`, VMOS path, gapid, renderdoc, PowerVR | `0x28ADC4` | alert flag patched, report path covered by sender patch |
| Frida | `frida-agent-32.so`, `frida-agent-64.so` | scanner near `0x24E544` | report path covered by sender patch |
| Xposed | `de.robv.android.xposed.installer`, `libxposed_art.so`, `XposedBridge.jar` | scanner near `0x252924` | report path covered by sender patch |
| Fake linker | `libfake-linker-21.so` through `libfake-linker-30.so` | scanner near `0x24F820` | report path covered by sender patch |
| JDWP | `JDWP Event Help`, `JDWP Command Re` | scanner near `0x250C24` | report path covered by sender patch |
| Proc scans | `/proc/%u/status`, `/proc/%d/task/%d/status`, wchan paths | scanners near `0x2505C8` and `0x250EBC` | report path covered by sender patch |
| Inline hook integrity | `inline_hook_opcode_dismatch`, `ms_hook_opcode` | logger near `0x36B0F8` | logged locally, no report sink found |
| Package checks | `com.ace.gamesafe4`, `com.ano.gshell.GP6Service` | strings at `0x27CDB0`, `0x2582A0` | no local kill path found, report path covered by sender patch |

The warning packet sender is the important addition. Ten report builders call `0x256FBC` to obtain the same report object, then call vtable slot 2 at `0x506DB8`. `readelf -r` resolves that slot to `0x2570B8`. Returning zero there stops every warning packet from those builders while keeping callers from retrying.

## Patch table

| Name | RVA | Original bytes | Patch bytes | Effect |
|---|---|---|---|---|
| `root_decision` | `0x250FF8` | `fd7bbea9f44f01a9` | `1f008052c0035fd6` | combined root decision returns 0 |
| `unlock_decision` | `0x2B3DA0` | `010200b4fd7bbea9` | `1f008052c0035fd6` | unlock decision returns 0 |
| `root_report` | `0x284420` | `fd7bbea9fc0b00f9` | `c05fd6031f2003d5` | root warning builder returns early |
| `unlock_report` | `0x2B3E08` | `fd7bbea9fc0b00f9` | `c05fd6031f2003d5` | unlock warning builder returns early |
| `mt2_no_root_branch` | `0x24A10C` | `88010037` | `1f2003d5` | config flag no longer sets detection byte |
| `root_telemetry` | `0x25097C` | `ff8306d1fd7b17a9` | `1f008052c0035fd6` | `is_root` telemetry reports 0 |
| `debugger_check_branch` | `0x25EAD8` | `a0020054` | `1f2003d5` | debugger checker skips ptrace block |
| `debugger_status_getter` | `0x25E9C8` | `007440b9c0035fd6` | `1f008052c0035fd6` | debugger status returns 0 |
| `debugger_flag_getter` | `0x25E9D0` | `00e04139c0035fd6` | `1f008052c0035fd6` | debugger flag returns 0 |
| `native_report_sink` | `0x2594AC` | `fd7bbca9fc0b00f9` | `c05fd6031f2003d5` | shared native report builder returns early |
| `warning_report_send` | `0x2570B8` | `000440f9600100b4` | `1f008052c0035fd6` | warning packet sender returns success without sending |
| `gpu_profiler_report` | `0x256B2C` | `fd7bbea9fc0b00f9` | `c05fd6031f2003d5` | GPU profiler warning builder returns early |
| `autoclick_report` | `0x27A8DC` | `fd7bbea9fc4f01a9` | `c05fd6031f2003d5` | AutoClicker warning builder returns early |
| `root_alert_flag` | `0x2D1700` | `ff0302d1fd7b06a9` | `c05fd6031f2003d5` | root alert setter returns early |
| `emulator_alert_flag` | `0x28ADC4` | `ff0302d1fd7b06a9` | `c05fd6031f2003d5` | emulator alert setter returns early |

## Usage

`ff_antidetect.py` applies all fifteen patches through ptrace. The module applies the same table in process when `meow.antidetect=1`; this is off by default because the external patcher is preferred.

```sh
su -c './run_antidetect.sh --check'
su -c './run_antidetect.sh'
su -c './run_antidetect.sh --restore'
```

Select individual patches with `--only warning_report_send gpu_profiler_report`.

## Caveats

- Patches are in process memory only. Reapply after every game launch.
- Game updates change every RVA. Re-derive them from the new `libanogs.so`.
- This audit does not claim to disable server-side anti-cheat or ban waves.
- The inline hook logger at `0x36B0F8` was left untouched because it is a local error logger, not a verified report sink.
