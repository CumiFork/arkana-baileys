#!/usr/bin/env bash
set -euo pipefail

PKG="com.mobile.legends"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
INJECTOR="$REPO_DIR/projek/inject"
SO_SRC="${1:-/data/local/tmp/libMEOW.so}"
CFG="/data/local/tmp/v2_autoretribution.cfg"
STATUS="/data/local/tmp/v2_autoretribution.status"
LOG_TAG="MEOW_V2"

usage() {
  echo "Usage: $0 [path/to/libMEOW.so]"
  echo "Default .so: /data/local/tmp/libMEOW.so"
  exit 1
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
fi

if [[ ! -f "$SO_SRC" ]]; then
  echo "[!] .so not found: $SO_SRC"
  exit 1
fi

if [[ ! -x "$INJECTOR" ]]; then
  echo "[!] injector not found or not executable: $INJECTOR"
  exit 1
fi

if ! su -c 'id' >/dev/null 2>&1; then
  echo "[!] root/su required"
  exit 1
fi

echo "[*] Writing config: $CFG"
su -c "cat > '$CFG' << 'CFG'
enabled=1
hp_percent=20
cooldown_ms=500
only_wild_monsters=1
debug_log=1
cast_enabled=0
required_skill_id=0
max_monster_scan=64
config_reload_sec=3

# Auto Retribution target IDs (set from debug log monster IDs)
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

# Aim (scaffold, needs verified offsets)
aim_enabled=0
aim_beatrix_only=0
aim_basic_attack=1
aim_fov=120
aim_smooth=8
aim_enemy_only=1

# ESP (scaffold, needs verified world-to-screen offsets)
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
CFG
chmod 644 '$CFG'"

PID="$(su -c "pidof $PKG" | tr -d '\r' | awk '{print $1}')"
if [[ -z "$PID" ]]; then
  echo "[!] $PKG is not running. Open MLBB first."
  exit 1
fi
echo "[*] Target PID: $PID"

# Stage .so in app lib dir for SELinux Enforcing (apk_data_file context)
LIBDIR="$(su -c "ls -d /data/app/*/${PKG}*/lib/arm64 2>/dev/null | head -1" | tr -d '\r')"
if [[ -z "$LIBDIR" ]]; then
  LIBDIR="$(su -c "ls -d /data/data/${PKG}/lib 2>/dev/null | head -1" | tr -d '\r')"
fi

if [[ -n "$LIBDIR" ]]; then
  DST="$LIBDIR/libMEOW.so"
  echo "[*] Staging .so to $DST"
  su -c "cp '$SO_SRC' '$DST' && chmod 755 '$DST'"
  # Try to restore expected context; ignore failure on non-SELinux setups
  su -c "chcon u:object_r:apk_data_file:s0 '$DST' 2>/dev/null || true"
  INJECT_PATH="$DST"
else
  echo "[!] App lib dir not found, falling back to /data/local/tmp"
  DST="/data/local/tmp/libMEOW.so"
  su -c "cp '$SO_SRC' '$DST' && chmod 755 '$DST'"
  INJECT_PATH="$DST"
fi

echo "[*] Injecting: $INJECT_PATH"
set +e
OUT="$(su -c "'$INJECTOR' '$PID' '$INJECT_PATH'" 2>&1)"
RC=$?
set -e
echo "$OUT"
if [[ $RC -ne 0 ]]; then
  echo "[!] Injector exited with code $RC"
  exit $RC
fi

echo "[*] Status file: $STATUS"
su -c "cat '$STATUS' 2>/dev/null || echo '(not created yet)'"
echo "[*] Tailing logcat ($LOG_TAG). Ctrl+C to stop."
su -c "logcat -c && logcat -s $LOG_TAG"