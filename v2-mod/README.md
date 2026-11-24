# v2-mod

Modul terpisah dari `v1/` untuk eksperimen MLBB dengan hook/injeksi. `v1/` tetap 0% inject/read-only.

## Status saat ini

- Menu ImGui in-game (render via `eglSwapBuffers` hook). Input sentuh dicoba lewat `UnityEngine.Input` runtime resolve; jika tidak aktif, ubah `menu_open` di config.
- Auto Retribution: deteksi HP monster lewat hook `BattleManager.Update`. Filter target: Lord/Turtle/Blue Buff/Purple Buff/Red Buff berdasarkan ID monster yang dikonfigurasi.
- Room info: baca camp/rank/room ID/player count/enemy count dari `ShowPlayer`/`BattleManager` (offset dump-derived, perlu verifikasi build target).
- ESP / Aim: UI dan config tersedia, tetapi **belum aktif menggambar/aim** karena offset camera/world-to-screen/transform/aim-write belum diverifikasi.
- Real auto-cast retribution: **belum diimplementasi** (`cast_enabled` safety gate).

## Build

Butuh Android NDK (`ndk-build`). Build via GitHub Actions workflow `.github/workflows/build-v2-mod.yml`.

Lokal (jika NDK tersedia):

```bash
cd v2-mod/src/main/jni
ndk-build NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=Android.mk NDK_APPLICATION_MK=Application.mk
```

Output: `v2-mod/src/main/jni/libs/arm64-v8a/libMEOW.so`

## Inject

```bash
cd /data/data/com.termux/files/home/arkana-baileys
bash v2-mod/loader.sh /path/to/libMEOW.so
```

Loader menulis config default, men-stage `.so` ke lib dir app MLBB (SELinux Enforcing), inject via `projek/inject`, lalu tail `logcat -s MEOW_V2`.

## Config

File: `/data/local/tmp/v2_autoretribution.cfg`

```ini
enabled=1
hp_percent=20
cooldown_ms=500
only_wild_monsters=1
debug_log=1
cast_enabled=0
required_skill_id=0
max_monster_scan=64
config_reload_sec=3

# Auto Retribution target filter
retri_lord=1
retri_turtle=1
retri_blue_buff=1
retri_purple_buff=1
retri_red_buff=1
id_lord=0
id_turtle=0
id_blue_buff=0
id_purple_buff=0
id_red_buff=0

# Aim (scaffold)
aim_enabled=0
aim_beatrix_only=0
aim_basic_attack=1
aim_fov=120
aim_smooth=8
aim_enemy_only=1

# ESP (scaffold)
esp_enabled=0
esp_box=1
esp_line=1
esp_name=1
esp_hp=1
esp_distance=0
esp_enemy_only=1
esp_hide_teammates=1

# Room info
room_enabled=1
room_show_rank=1
room_show_team=1
room_show_id=0

menu_open=1
```

Untuk membedakan Lord/Turtle/Buff, isi `id_*` dengan monster ID dari log debug (`MEOW_V2`) saat target terdeteksi.

## Status

File: `/data/local/tmp/v2_autoretribution.status`. Berisi state hook, retri, camp/rank/room/player/enemy count, dan flag fitur.

## Catatan penting

- Ini modul injeksi/hook; gunakan hanya pada environment yang kamu miliki/kendalikan.
- `cast_enabled` default `0`; path cast masih TODO.
- ESP/aim butuh offset camera/world-to-screen/transform build target. Jangan anggap aktif hanya karena toggle ada.
- Jika menu tidak bisa disentuh, set `menu_open=1/0` di config lalu tunggu reload.