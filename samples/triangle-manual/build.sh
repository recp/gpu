#!/bin/zsh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SAMPLE_DIR="$(cd "$(dirname "$0")" && pwd)"
OUT_BIN="$SAMPLE_DIR/hello-triangle-manual"
SDK_PATH="$(xcrun --sdk macosx --show-sdk-path)"

source "$ROOT/samples/common/build-library.sh"
gpu_build_library "$ROOT" metal

xcrun --sdk macosx clang \
  -fobjc-arc \
  -isysroot "$SDK_PATH" \
  -I"$ROOT/include" \
  -framework AppKit \
  -framework Metal \
  -framework Foundation \
  -framework QuartzCore \
  "$SAMPLE_DIR/main.m" \
  -L"$LIB_DIR" \
  -lgpu \
  -Wl,-rpath,"$LIB_DIR" \
  -o "$OUT_BIN"

echo "Built $OUT_BIN"
