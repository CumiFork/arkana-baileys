#!/data/data/com.termux/files/usr/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$ROOT/.." && pwd)"
BUILD_SO="$ROOT/build/termux/libs/arm64-v8a/libMEOW.so"
BASE_APK="$ROOT/build/bin/Mlbb MapHack Imgui.apk"
OUT_APK="$ROOT/build/termux/Mlbb-ImGui-AutoRetribution.apk"
KEYSTORE="${HOME}/.android/debug.keystore"
WORK="/data/data/com.termux/files/usr/tmp/opencode/v1-load-ml"

if [ ! -f "$BUILD_SO" ]; then
  echo "[*] libMEOW.so belum ada, build dulu..."
  bash "$ROOT/build-termux.sh"
fi

for cmd in zip zipalign apksigner; do
  command -v "$cmd" >/dev/null 2>&1 || { echo "[-] butuh: $cmd"; exit 1; }
done

[ -f "$BASE_APK" ] || { echo "[-] Base APK tidak ditemukan: $BASE_APK"; exit 1; }
[ -f "$KEYSTORE" ] || { echo "[-] Debug keystore tidak ditemukan: $KEYSTORE"; exit 1; }

rm -rf "$WORK"
mkdir -p "$WORK/lib/arm64-v8a"
cp "$BUILD_SO" "$WORK/lib/arm64-v8a/libMEOW.so"
cp "$BASE_APK" "$WORK/base.apk"

(
  cd "$WORK"
  zip -0 -q -r base.apk lib/arm64-v8a/libMEOW.so
  zipalign -f -p 4 base.apk aligned.apk
  apksigner sign \
    --ks "$KEYSTORE" \
    --ks-pass pass:android \
    --key-pass pass:android \
    --out signed.apk \
    aligned.apk
)

mkdir -p "$(dirname "$OUT_APK")"
cp "$WORK/signed.apk" "$OUT_APK"

echo "[+] APK siap: $OUT_APK"
echo "[*] Install (root): su -c 'pm install -r \"$OUT_APK\"'"
echo "[*] Atau install biasa: termux-open \"$OUT_APK\""

if [ "${1:-}" = "--install" ]; then
  if su -c "pm install -r '$OUT_APK'" >/dev/null 2>&1; then
    echo "[+] APK terinstall."
  else
    echo "[-] Gagal install via su/pm. Coba buka APK manual."
  fi
fi
