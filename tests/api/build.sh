#!/bin/zsh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEST_DIR="$(cd "$(dirname "$0")" && pwd)"
DERIVED_DATA="${GPU_DERIVED_DATA:-/tmp/gpu-dd}"
LIB_DIR="$DERIVED_DATA/Build/Products/Debug"
OUT_BIN="$TEST_DIR/api-tests"
SDK_PATH="$(xcrun --sdk macosx --show-sdk-path)"

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
  "$TEST_DIR"/*.c \
  -L"$LIB_DIR" \
  -lgpu \
  -Wl,-rpath,"$LIB_DIR" \
  -o "$OUT_BIN"

"$OUT_BIN"
