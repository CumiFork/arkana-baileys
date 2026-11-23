#!/system/bin/sh
# libMEOW.sh - stage libMEOW.so for injection on rooted Android / Termux
# Usage: sh libMEOW.sh [package_name]

set -e

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SO="$SCRIPT_DIR/libMEOW.so"
PKG="${1:-com.dts.freefireth}"
OUT="/data/local/tmp/libMEOW.so"

if [ ! -f "$SO" ]; then
  echo "[!] libMEOW.so not found: $SO"
  exit 1
fi

if command -v su >/dev/null 2>&1; then
  su -c "cp '$SO' '$OUT' && chmod 755 '$OUT'"
  echo "[+] Copied to $OUT"
  PID=$(su -c "pidof $PKG" 2>/dev/null | tr -d '\r' | awk '{print $1}')
  if [ -n "$PID" ]; then
    echo "[+] $PKG pid=$PID"
    echo "[*] libMEOW.so siap di-inject. Gunakan injector ptrace/root favorit Anda ke PID $PID:"
    echo "    $OUT"
  else
    echo "[*] $PKG belum berjalan. Jalankan game, lalu inject:"
    echo "    $OUT"
  fi
else
  echo "[!] su tidak ditemukan. Jalankan di environment root."
  exit 1
fi
