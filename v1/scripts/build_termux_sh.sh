#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CPP_DIR="$ROOT_DIR/src/main/cpp"
BUILD_DIR="$ROOT_DIR/build/termux-cli"
DIST_DIR="$ROOT_DIR/dist"
OUT_SH="$DIST_DIR/native_overlay.sh"
API_LEVEL="${ANDROID_API_LEVEL:-24}"

mkdir -p "$BUILD_DIR" "$DIST_DIR"

detect_compiler() {
  if command -v clang++ >/dev/null 2>&1 && [[ -n "${PREFIX:-}" && "$(command -v clang++)" == "$PREFIX"* ]]; then
    echo "clang++"
    return 0
  fi
  if [[ -n "${ANDROID_NDK_HOME:-}" && -d "$ANDROID_NDK_HOME" ]]; then
    local host_prebuilt=""
    if [[ -d "$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin" ]]; then
      host_prebuilt="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin"
    elif [[ -d "$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/darwin-x86_64/bin" ]]; then
      host_prebuilt="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/darwin-x86_64/bin"
    fi
    if [[ -n "$host_prebuilt" ]]; then
      local cxx="$host_prebuilt/aarch64-linux-android${API_LEVEL}-clang++"
      if [[ -x "$cxx" ]]; then
        echo "$cxx"
        return 0
      fi
    fi
  fi
  if command -v clang++ >/dev/null 2>&1; then
    echo "clang++"
    return 0
  fi
  echo ""
  return 1
}

CXX="$(detect_compiler)"
if [[ -z "$CXX" ]]; then
  echo "clang++ not found. Install clang in Termux or set ANDROID_NDK_HOME." >&2
  exit 1
fi

echo "Using compiler: $CXX"

COMMON_FLAGS=(
  -std=c++17
  -O2
  -fPIC
  -fvisibility=hidden
  -DBUILD_TERMUX_CLI
  -I"$CPP_DIR"
)

SO_OUT="$BUILD_DIR/libnativecore_cli.so"
RUNNER_OUT="$BUILD_DIR/runner"

"$CXX" "${COMMON_FLAGS[@]}" -shared \
  "$CPP_DIR/core_engine.cpp" \
  -o "$SO_OUT" \
  -ldl

"$CXX" -std=c++17 -O2 -fPIE -pie -fvisibility=hidden \
  -I"$CPP_DIR" \
  "$CPP_DIR/termux_runner.cpp" \
  -o "$RUNNER_OUT" \
  -ldl

strip --strip-unneeded "$SO_OUT" 2>/dev/null || true
strip --strip-unneeded "$RUNNER_OUT" 2>/dev/null || true

PAYLOAD_TGZ="$BUILD_DIR/payload.tar.gz"
tar -C "$BUILD_DIR" -czf "$PAYLOAD_TGZ" "$(basename "$SO_OUT")" "$(basename "$RUNNER_OUT")"
PAYLOAD_B64="$BUILD_DIR/payload.b64"
base64 < "$PAYLOAD_TGZ" | tr -d '\n' > "$PAYLOAD_B64"

{
  cat <<'SCRIPT_HEAD'
#!/bin/sh
set -e

CACHE_DIR="${HOME:-/data/data/com.termux/files/home}/.cache/native_overlay"
mkdir -p "$CACHE_DIR"

PAYLOAD_B64="
SCRIPT_HEAD
  cat "$PAYLOAD_B64"
  cat <<'SCRIPT_TAIL'
"

PAYLOAD_TGZ="$CACHE_DIR/payload.tar.gz"
printf '%s' "$PAYLOAD_B64" | base64 -d > "$PAYLOAD_TGZ"
tar -xzf "$PAYLOAD_TGZ" -C "$CACHE_DIR"
rm -f "$PAYLOAD_TGZ"

chmod +x "$CACHE_DIR/runner" "$CACHE_DIR/libnativecore_cli.so" 2>/dev/null || true
export LD_LIBRARY_PATH="$CACHE_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

cd "$CACHE_DIR"
exec "$CACHE_DIR/runner" "$@"
SCRIPT_TAIL
} > "$OUT_SH"

chmod +x "$OUT_SH"

echo "Built: $SO_OUT"
echo "Built: $RUNNER_OUT"
echo "Packed: $OUT_SH"
