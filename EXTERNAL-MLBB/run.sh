#!/system/bin/sh
# EXTERNAL-MLBB runner (Android 14 / arm64-v8a, root required)
# Usage: su -c "sh /data/local/tmp/run.sh"

set -eu

MODDIR="/data/local/tmp/external-mlbb"
BIN="$MODDIR/Starcool.sh"

log() { printf '[EXTERNAL-MLBB] %s\n' "$*"; }
err() { printf '[EXTERNAL-MLBB][ERR] %s\n' "$*" >&2; }

# Run in sanitized root environment
unset LD_LIBRARY_PATH LD_PRELOAD || true
export PATH="/system/bin:/system/xbin:/vendor/bin:/product/bin:/apex/com.android.runtime/bin:/apex/com.android.art/bin:${PATH:-}"

# Ensure /data/local/tmp is accessible
mkdir -p "$MODDIR" 2>/dev/null || true
chmod 755 /data/local/tmp "$MODDIR" 2>/dev/null || true

# Android runtime classpath (for app_process/surface helpers if needed)
if [ -z "${BOOTCLASSPATH:-}" ]; then
  BOOTCLASSPATH="$(getprop ro.boot.class.path 2>/dev/null || true)"
  [ -n "$BOOTCLASSPATH" ] && export BOOTCLASSPATH
fi
if [ -z "${DEX2OATBOOTCLASSPATH:-}" ]; then
  DEX2OATBOOTCLASSPATH="$(getprop dalvik.vm.boot-dex2oat-class-path 2>/dev/null || true)"
  [ -n "$DEX2OATBOOTCLASSPATH" ] && export DEX2OATBOOTCLASSPATH
fi

# Screen resolution fallback: wm size -> dumpsys window displays -> defaults
WIDTH=""
HEIGHT=""
WM_SIZE="$(wm size 2>/dev/null | tr -d '\r' || true)"
# matches "Physical size: 1080x2400" or "Override size: ..."
RES_LINE="$(printf '%s\n' "$WM_SIZE" | grep -Eo '[0-9]+x[0-9]+' | tail -n 1 || true)"
if [ -n "$RES_LINE" ]; then
  WIDTH="${RES_LINE%x*}"
  HEIGHT="${RES_LINE#*x}"
fi
if [ -z "$WIDTH" ] || [ -z "$HEIGHT" ]; then
  DUMP_RES="$(dumpsys window displays 2>/dev/null | grep -Eo 'cur=[0-9]+x[0-9]+' | head -n 1 | sed 's/cur=//' || true)"
  if [ -n "$DUMP_RES" ]; then
    WIDTH="${DUMP_RES%x*}"
    HEIGHT="${DUMP_RES#*x}"
  fi
fi
WIDTH="${WIDTH:-1080}"
HEIGHT="${HEIGHT:-2400}"
export DISPLAY_WIDTH="$WIDTH"
export DISPLAY_HEIGHT="$HEIGHT"

if [ "$(id -u 2>/dev/null || echo 1)" -ne 0 ]; then
  err "This script must run as root. Use: su -c \"sh $0\""
  exit 1
fi

if [ ! -f "$BIN" ]; then
  # Allow running from current directory when not installed to /data/local/tmp
  if [ -f "./Starcool.sh" ]; then
    BIN="./Starcool.sh"
  else
    err "Binary not found: $BIN"
    exit 1
  fi
fi

chmod 755 "$BIN" 2>/dev/null || true

log "Resolution: ${WIDTH}x${HEIGHT}"
log "Starting: $BIN"
exec "$BIN" "$@"
