#!/bin/zsh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SAMPLE_DIR="$(cd "$(dirname "$0")" && pwd)"
DERIVED_DATA="${GPU_DERIVED_DATA:-/tmp/gpu-dd}"
LIB_DIR="$DERIVED_DATA/Build/Products/Debug"
OUT_BIN="$SAMPLE_DIR/usl-reflection-check"
SDK_PATH="$(xcrun --sdk macosx --show-sdk-path)"
US_ROOT="${USL_ROOT:-/Users/recp/Projects/recp/UniversalShading/us}"
USTEST="${USL_USTEST:-$US_ROOT/build/ustest}"
US_LIB_DIR="$US_ROOT/build/us"
US_DS_LIB_DIR="$US_LIB_DIR/deps/ds"
EMBED_METAL="${GPU_USL_EMBED_METAL:-0}"
LOCK_DIR="$SAMPLE_DIR/.build.lock"

while ! mkdir "$LOCK_DIR" 2>/dev/null; do
  sleep 0.1
done
trap 'rmdir "$LOCK_DIR"' EXIT

if [[ ! -x "$USTEST" ]]; then
  echo "ustest not found at $USTEST" >&2
  exit 1
fi

rm -f "$SAMPLE_DIR/reflection.us" "$SAMPLE_DIR/reflection.usl.metal"

ustest_cmd=("$USTEST" --shader "$SAMPLE_DIR/reflection.usl" --no-logs --no-sidecar)
expected_source_kind="generated"
if [[ "$EMBED_METAL" == "1" ]]; then
  expected_source_kind="embedded"
  USL_EMBED_METAL_BLOB=1 "${ustest_cmd[@]}" >/tmp/gpu-usl-reflection-check-ustest.log 2>&1
else
  "${ustest_cmd[@]}" >/tmp/gpu-usl-reflection-check-ustest.log 2>&1
fi

if [[ ! -f "$SAMPLE_DIR/reflection.us" ]]; then
  echo "USL bytecode artifact was not generated: $SAMPLE_DIR/reflection.us" >&2
  exit 1
fi

if [[ -f "$SAMPLE_DIR/reflection.usl.metal" ]]; then
  echo "USL Metal sidecar should not be generated in --no-sidecar mode" >&2
  exit 1
fi

xcodebuild \
  -project "$ROOT/gpu.xcodeproj" \
  -scheme gpu \
  -configuration Debug \
  -derivedDataPath "$DERIVED_DATA" \
  CODE_SIGNING_ALLOWED=NO \
  CODE_SIGNING_REQUIRED=NO \
  build

xcrun --sdk macosx clang \
  -std=c11 \
  -Wall \
  -Wextra \
  -Werror \
  -isysroot "$SDK_PATH" \
  -I"$ROOT/include" \
  "$SAMPLE_DIR/main.c" \
  -L"$LIB_DIR" \
  -lgpu \
  -Wl,-rpath,"$LIB_DIR" \
  -Wl,-rpath,"$US_LIB_DIR" \
  -Wl,-rpath,"$US_DS_LIB_DIR" \
  -o "$OUT_BIN"

"$OUT_BIN" "$SAMPLE_DIR/reflection.us" "$expected_source_kind"
