#!/bin/zsh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SAMPLE_DIR="$(cd "$(dirname "$0")" && pwd)"
VULKAN_SDK="${VULKAN_SDK:-$HOME/Library/Developer/VulkanSDK/1.4.350.1}"
US_ROOT="${USL_ROOT:-/Users/recp/Projects/recp/UniversalShading/us}"
USTEST="${USL_USTEST:-$US_ROOT/build/ustest}"
SDK_PATH="$(xcrun --sdk macosx --show-sdk-path)"
OUT_BIN="$SAMPLE_DIR/hello-compute-buffer-vulkan-usl"
RENDER_OUT_BIN="$SAMPLE_DIR/hello-compute-render-vulkan-usl"

"$USTEST" --shader "$SAMPLE_DIR/compute_buffer.usl" --no-logs --no-sidecar \
  >/tmp/gpu-compute-buffer-vulkan-usl-ustest.log 2>&1

source "$ROOT/samples/common/build-library.sh"
gpu_build_library "$ROOT" vulkan

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
  -Wl,-rpath,"$US_LIB_DIR/deps/ds" \
  -Wl,-rpath,"$VULKAN_SDK/macOS/lib" \
  -o "$OUT_BIN"

xcrun --sdk macosx clang \
  -fobjc-arc \
  -DGPU_SAMPLE_BACKEND=GPU_BACKEND_VULKAN \
  -isysroot "$SDK_PATH" \
  -I"$ROOT/include" \
  -framework AppKit \
  -framework Foundation \
  -framework QuartzCore \
  "$ROOT/samples/compute-buffer-usl/main.m" \
  -L"$LIB_DIR" \
  -lgpu \
  -Wl,-rpath,"$LIB_DIR" \
  -Wl,-rpath,"$US_LIB_DIR" \
  -Wl,-rpath,"$US_LIB_DIR/deps/ds" \
  -Wl,-rpath,"$VULKAN_SDK/macOS/lib" \
  -o "$RENDER_OUT_BIN"

echo "Built $OUT_BIN"
echo "Built $RENDER_OUT_BIN"
