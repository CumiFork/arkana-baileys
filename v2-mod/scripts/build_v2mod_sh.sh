#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
JNI_DIR="$ROOT_DIR/v2-mod/src/main/jni"
OUT_SH="$ROOT_DIR/v2mod.sh"

NDK_BUILD_BIN="${NDK_BUILD_BIN:-ndk-build}"
if ! command -v "$NDK_BUILD_BIN" >/dev/null 2>&1; then
  echo "ndk-build not found in PATH" >&2
  exit 1
fi

pushd "$JNI_DIR" >/dev/null
"$NDK_BUILD_BIN" NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=Android.mk NDK_APPLICATION_MK=Application.mk APP_ABI=arm64-v8a APP_PLATFORM=android-24 -j"$(nproc 2>/dev/null || echo 4)"
popd >/dev/null

BIN_PATH="$JNI_DIR/libs/arm64-v8a/v2mod"
if [ ! -f "$BIN_PATH" ]; then
  echo "Built binary not found: $BIN_PATH" >&2
  exit 1
fi

B64=$(base64 -w0 "$BIN_PATH")
cat > "$OUT_SH" <<'SCRIPT'
#!/system/bin/sh
# Self-extracting v2-mod standalone native overlay
set -eu
PAYLOAD_BIN="v2mod.bin"
OUT_DIR="/data/local/tmp"
OUT_BIN="$OUT_DIR/$PAYLOAD_BIN"
TMP_B64="$OUT_DIR/.v2mod.b64.$$"
cleanup() { rm -f "$TMP_B64" "$OUT_BIN" 2>/dev/null || true; }
trap cleanup EXIT INT TERM
extract_payload() {
  cat > "$TMP_B64" <<'__PAYLOAD__'
SCRIPT
printf '%s\n' "$B64" >> "$OUT_SH"
cat >> "$OUT_SH" <<'SCRIPT'
__PAYLOAD__
  if command -v base64 >/dev/null 2>&1; then base64 -d "$TMP_B64" > "$OUT_BIN";
  elif command -v busybox >/dev/null 2>&1; then busybox base64 -d "$TMP_B64" > "$OUT_BIN";
  else echo "[!] base64 decoder not found" >&2; exit 1; fi
  rm -f "$TMP_B64"; chmod 755 "$OUT_BIN"
}
SCRIPT_PATH="$0"
if command -v readlink >/dev/null 2>&1; then SCRIPT_PATH=$(readlink -f "$0" 2>/dev/null || echo "$0"); fi
if [ "$(id -u 2>/dev/null || echo 1)" -ne 0 ]; then
  if command -v su >/dev/null 2>&1; then echo "[*] Root required, re-exec via su"; exec su -c "sh '$SCRIPT_PATH'"; fi
  echo "[!] Root not available. Run: su -c 'sh $SCRIPT_PATH'" >&2; exit 1
fi
export PATH="/system/bin:/system/xbin:/vendor/bin:/sbin:$PATH"
echo "[*] Extracting v2mod standalone to $OUT_BIN"
extract_payload
echo "[*] Starting v2mod overlay (Ctrl+C to exit)"
"$OUT_BIN" "$@"
STATUS=$?
echo "[*] v2mod exited with status $STATUS"
exit $STATUS
SCRIPT
chmod +x "$OUT_SH"
echo "Wrote $OUT_SH"
