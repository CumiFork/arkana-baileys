#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
JNI_DIR="$ROOT_DIR/v2-mod/src/main/jni"
JAVA_SRC="$ROOT_DIR/v2-mod/src/main/java"
OUT_SH="$ROOT_DIR/v2mod.sh"

NDK_BUILD_BIN="${NDK_BUILD_BIN:-ndk-build}"
if ! command -v "$NDK_BUILD_BIN" >/dev/null 2>&1; then
  echo "ndk-build not found in PATH" >&2
  exit 1
fi

ANDROID_JAR="${ANDROID_JAR:-}"
if [ -z "$ANDROID_JAR" ]; then
  if [ -n "${ANDROID_HOME:-}" ] && [ -f "$ANDROID_HOME/platforms/android-34/android.jar" ]; then
    ANDROID_JAR="$ANDROID_HOME/platforms/android-34/android.jar"
  elif [ -n "${ANDROID_SDK_ROOT:-}" ] && [ -f "$ANDROID_SDK_ROOT/platforms/android-34/android.jar" ]; then
    ANDROID_JAR="$ANDROID_SDK_ROOT/platforms/android-34/android.jar"
  fi
fi
if [ -z "$ANDROID_JAR" ] || [ ! -f "$ANDROID_JAR" ]; then
  echo "ANDROID_JAR not found (set ANDROID_JAR or ANDROID_HOME)" >&2
  exit 1
fi

D8_BIN="${D8_BIN:-}"
if [ -z "$D8_BIN" ]; then
  if [ -n "${ANDROID_HOME:-}" ]; then
    D8_BIN=$(ls -1 "$ANDROID_HOME"/build-tools/*/d8 2>/dev/null | sort -V | tail -n1 || true)
  fi
fi
if [ -z "$D8_BIN" ] || [ ! -x "$D8_BIN" ]; then
  echo "d8 not found (set D8_BIN or ANDROID_HOME)" >&2
  exit 1
fi

pushd "$JNI_DIR" >/dev/null
"$NDK_BUILD_BIN" NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=Android.mk NDK_APPLICATION_MK=Application.mk APP_ABI=arm64-v8a APP_PLATFORM=android-24 -j"$(nproc 2>/dev/null || echo 4)"
popd >/dev/null

SO_PATH="$JNI_DIR/libs/arm64-v8a/libv2mod.so"
if [ ! -f "$SO_PATH" ]; then
  echo "Built shared library not found: $SO_PATH" >&2
  exit 1
fi

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT
CLASSES_DIR="$WORK_DIR/classes"
mkdir -p "$CLASSES_DIR"
javac --release 17 -classpath "$ANDROID_JAR" -d "$CLASSES_DIR" "$JAVA_SRC/com/android/support/V2Overlay.java"
"$D8_BIN" --output "$WORK_DIR" "$CLASSES_DIR"/com/android/support/*.class
DEX_PATH="$WORK_DIR/classes.dex"
if [ ! -f "$DEX_PATH" ]; then
  echo "classes.dex not found" >&2
  exit 1
fi

SO_B64=$(base64 -w0 "$SO_PATH")
DEX_B64=$(base64 -w0 "$DEX_PATH")

cat > "$OUT_SH" <<'SCRIPT'
#!/system/bin/sh
# Self-extracting v2-mod standalone overlay (Android 14 / HyperOS 1 compatible)
# Uses app_process + JNI SurfaceControl, no private libgui ABI.
set -eu

OUT_DIR="/data/local/tmp"
SO_NAME="libv2mod.so"
DEX_NAME="v2mod.dex"
OUT_SO="$OUT_DIR/$SO_NAME"
OUT_DEX="$OUT_DIR/$DEX_NAME"
TMP_SO_B64="$OUT_DIR/.v2mod.so.b64.$$"
TMP_DEX_B64="$OUT_DIR/.v2mod.dex.b64.$$"

cleanup() {
  rm -f "$TMP_SO_B64" "$TMP_DEX_B64" "$OUT_SO" "$OUT_DEX" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

decode_b64() {
  _in="$1"; _out="$2"
  if command -v base64 >/dev/null 2>&1; then
    base64 -d "$_in" > "$_out"
  elif command -v busybox >/dev/null 2>&1; then
    busybox base64 -d "$_in" > "$_out"
  else
    echo "[!] base64 decoder not found" >&2
    exit 1
  fi
}

extract_payloads() {
  cat > "$TMP_SO_B64" <<'__V2MOD_SO__'
SCRIPT
printf '%s\n' "$SO_B64" >> "$OUT_SH"
cat >> "$OUT_SH" <<'SCRIPT'
__V2MOD_SO__
  cat > "$TMP_DEX_B64" <<'__V2MOD_DEX__'
SCRIPT
printf '%s\n' "$DEX_B64" >> "$OUT_SH"
cat >> "$OUT_SH" <<'SCRIPT'
__V2MOD_DEX__
  decode_b64 "$TMP_SO_B64" "$OUT_SO"
  decode_b64 "$TMP_DEX_B64" "$OUT_DEX"
  rm -f "$TMP_SO_B64" "$TMP_DEX_B64"
  chmod 755 "$OUT_SO"
  chmod 644 "$OUT_DEX"
  # Best-effort SELinux context for Android 14/Enforcing
  chcon u:object_r:system_file:s0 "$OUT_SO" 2>/dev/null || \
    chcon u:object_r:apk_data_file:s0 "$OUT_SO" 2>/dev/null || true
  chcon u:object_r:system_file:s0 "$OUT_DEX" 2>/dev/null || \
    chcon u:object_r:apk_data_file:s0 "$OUT_DEX" 2>/dev/null || true
}

SCRIPT_PATH="$0"
if command -v readlink >/dev/null 2>&1; then
  _rp=$(readlink -f "$0" 2>/dev/null || true)
  if [ -n "$_rp" ]; then SCRIPT_PATH="$_rp"; fi
fi

if [ "$(id -u 2>/dev/null || echo 1)" -ne 0 ]; then
  if command -v su >/dev/null 2>&1; then
    echo "[*] Root required, re-exec via su"
    exec su -c "sh '$SCRIPT_PATH' $*"
  fi
  echo "[!] Root not available. Run: su -c 'sh $SCRIPT_PATH'" >&2
  exit 1
fi

# Get display size (wm size) for Java/native initial sizing
DISP_W=""
DISP_H=""
WM_LINE=$(wm size 2>/dev/null | tail -n1 || true)
if [ -n "$WM_LINE" ]; then
  DISP_WH=$(echo "$WM_LINE" | tr -cd '0-9x' | sed -n 's/.*\([0-9][0-9]*x[0-9][0-9]*\).*/\1/p')
  case "$DISP_WH" in
    *x*)
      DISP_W=${DISP_WH%x*}
      DISP_H=${DISP_WH#*x}
      ;;
  esac
fi

echo "[*] Extracting v2mod to $OUT_DIR"
extract_payloads

APP_PROCESS="/system/bin/app_process"
if [ ! -x "$APP_PROCESS" ] && [ -x "/system/bin/app_process64" ]; then
  APP_PROCESS="/system/bin/app_process64"
fi
if [ ! -x "$APP_PROCESS" ]; then
  echo "[!] app_process not found" >&2
  exit 1
fi

# Capture Android runtime env vars before sanitizing (needed by app_process).
BCP=""
ZPID=$(pidof zygote64 2>/dev/null | awk '{print $1}' || true)
if [ -z "$ZPID" ]; then ZPID=$(pidof zygote 2>/dev/null | awk '{print $1}' || true); fi
if [ -n "$ZPID" ] && [ -r "/proc/$ZPID/environ" ]; then
  BCP=$(tr '\0' '\n' < "/proc/$ZPID/environ" 2>/dev/null | grep '^BOOTCLASSPATH=' | head -n1 | sed 's/^BOOTCLASSPATH=//' || true)
fi
SSCP=""
if [ -n "$ZPID" ] && [ -r "/proc/$ZPID/environ" ]; then
  SSCP=$(tr '\0' '\n' < "/proc/$ZPID/environ" 2>/dev/null | grep '^SYSTEMSERVERCLASSPATH=' | head -n1 | sed 's/^SYSTEMSERVERCLASSPATH=//' || true)
fi

echo "[*] Starting v2mod overlay (Ctrl+C to exit)"
# Sanitize Termux env to avoid symbol/library conflicts on Android 14/HyperOS
# env -i clears LD_LIBRARY_PATH/LD_PRELOAD inherited from Termux.
STATUS=0
if [ -n "$DISP_W" ] && [ -n "$DISP_H" ]; then
  env -i \
    PATH=/system/bin:/system/xbin \
    ANDROID_DATA=/data \
    ANDROID_ROOT=/system \
    ANDROID_ART_ROOT=/apex/com.android.art \
    ANDROID_I18N_ROOT=/apex/com.android.i18n \
    ANDROID_RUNTIME_ROOT=/apex/com.android.runtime \
    ANDROID_TZDATA_ROOT=/apex/com.android.tzdata \
    BOOTCLASSPATH="$BCP" \
    SYSTEMSERVERCLASSPATH="$SSCP" \
    CLASSPATH="$OUT_DEX" \
    "$APP_PROCESS" /system/bin com.android.support.V2Overlay --width "$DISP_W" --height "$DISP_H" || STATUS=$?
else
  env -i \
    PATH=/system/bin:/system/xbin \
    ANDROID_DATA=/data \
    ANDROID_ROOT=/system \
    ANDROID_ART_ROOT=/apex/com.android.art \
    ANDROID_I18N_ROOT=/apex/com.android.i18n \
    ANDROID_RUNTIME_ROOT=/apex/com.android.runtime \
    ANDROID_TZDATA_ROOT=/apex/com.android.tzdata \
    BOOTCLASSPATH="$BCP" \
    SYSTEMSERVERCLASSPATH="$SSCP" \
    CLASSPATH="$OUT_DEX" \
    "$APP_PROCESS" /system/bin com.android.support.V2Overlay || STATUS=$?
fi
echo "[*] v2mod exited with status $STATUS"
exit "$STATUS"
STATUS=$?
echo "[*] v2mod exited with status $STATUS"
exit $STATUS
SCRIPT
chmod +x "$OUT_SH"
echo "Wrote $OUT_SH"