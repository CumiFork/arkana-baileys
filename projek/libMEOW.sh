#!/system/bin/sh
# libMEOW.sh - stage and inject libMEOW.so on rooted Android / Termux
# Usage: sh libMEOW.sh [package_name]

set -e

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SO="$SCRIPT_DIR/libMEOW.so"
INJECTOR="$SCRIPT_DIR/inject"
PKG="${1:-com.dts.freefireth}"
OUT="/data/local/tmp/libMEOW.so"

if [ ! -f "$SO" ]; then
  echo "[!] libMEOW.so not found: $SO"
  exit 1
fi

if ! command -v su >/dev/null 2>&1; then
  echo "[!] su tidak ditemukan. Jalankan di environment root."
  exit 1
fi

su -c "cp '$SO' '$OUT' && chmod 755 '$OUT'"
echo "[+] Copied to $OUT"

PID=$(su -c "pidof $PKG" 2>/dev/null | tr -d '\r' | awk '{print $1}')
if [ -z "$PID" ]; then
  echo "[*] $PKG belum berjalan. Jalankan game, lalu jalankan ulang script ini."
  exit 0
fi

echo "[+] $PKG pid=$PID"
if [ -x "$INJECTOR" ]; then
  echo "[*] Injecting $OUT -> pid $PID ..."
  su -c "'$INJECTOR' $PID '$OUT'"
  echo "[+] Selesai. Cek overlay/menu di game."
else
  echo "[*] Injector tidak ditemukan ($INJECTOR). Inject manual ke PID $PID:"
  echo "    $OUT"
fi
