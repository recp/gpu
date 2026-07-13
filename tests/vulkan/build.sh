#!/bin/zsh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEST_DIR="$(cd "$(dirname "$0")" && pwd)"
VULKAN_SDK="${VULKAN_SDK:-$HOME/Library/Developer/VulkanSDK/1.4.350.1}"
USTEST="${USL_USTEST:-${USL_ROOT:-/Users/recp/Projects/recp/UniversalShading/us}/build/ustest}"
ICD="$VULKAN_SDK/macOS/share/vulkan/icd.d/MoltenVK_icd.json"
SDK_PATH="$(xcrun --sdk macosx --show-sdk-path)"

source "$ROOT/samples/common/build-library.sh"
gpu_build_library "$ROOT" vulkan

build_test() {
  local source="$1"
  local output="$2"
  shift 2

  xcrun --sdk macosx clang \
    -std=c11 \
    -Wall \
    -Wextra \
    -Werror \
    -isysroot "$SDK_PATH" \
    -I"$ROOT/include" \
    -I"$ROOT/src" \
    -I"$VULKAN_SDK/macOS/include" \
    "$source" \
    "$@" \
    -L"$LIB_DIR" \
    -lgpu \
    -Wl,-rpath,"$LIB_DIR" \
    -Wl,-rpath,"$US_LIB_DIR" \
    -Wl,-rpath,"$VULKAN_SDK/macOS/lib" \
    -o "$output"
}

build_test "$TEST_DIR/queue.c" "$TEST_DIR/vulkan-queue"
build_test "$TEST_DIR/shader.c" \
  "$TEST_DIR/vulkan-shader" \
  "$ROOT/tests/api/copy.c" \
  "$ROOT/tests/api/texture_transfer.c" \
  "$TEST_DIR/texture.c"

COMPUTE_SAMPLE="$ROOT/samples/compute-buffer-vulkan-usl"
MRT_FIXTURE="${GPU_VULKAN_MRT_FIXTURE:-/tmp/gpu-vulkan-render-mrt}"
TEXTURE_FIXTURE="${GPU_VULKAN_TEXTURE_FIXTURE:-/tmp/gpu-vulkan-textured-quad}"
rm -rf "$MRT_FIXTURE"
rm -rf "$TEXTURE_FIXTURE"
mkdir -p "$MRT_FIXTURE"
mkdir -p "$TEXTURE_FIXTURE"
cp "$ROOT/tests/api/render_mrt.usl" "$MRT_FIXTURE/render_mrt.usl"
cp "$ROOT/samples/textured-quad-vulkan-usl/textured_quad.usl" \
  "$TEXTURE_FIXTURE/textured_quad.usl"
"$USTEST" --shader "$MRT_FIXTURE/render_mrt.usl" \
  --no-logs --no-sidecar >/tmp/gpu-vulkan-mrt-ustest.log 2>&1
"$USTEST" --shader "$TEXTURE_FIXTURE/textured_quad.usl" \
  --no-logs --no-sidecar >/tmp/gpu-vulkan-texture-ustest.log 2>&1
"$USTEST" --shader "$COMPUTE_SAMPLE/compute_buffer.usl" \
  --no-logs --no-sidecar >/tmp/gpu-vulkan-compute-ustest.log 2>&1
build_test "$COMPUTE_SAMPLE/main.c" \
  "$COMPUTE_SAMPLE/hello-compute-buffer-vulkan-usl"

VK_ICD_FILENAMES="$ICD" "$TEST_DIR/vulkan-queue"
VK_ICD_FILENAMES="$ICD" \
  "$TEST_DIR/vulkan-shader" \
  "$ROOT/samples/triangle-usl/triangle.us" \
  "$MRT_FIXTURE/render_mrt.us" \
  "$TEXTURE_FIXTURE/textured_quad.us"
VK_ICD_FILENAMES="$ICD" \
  "$COMPUTE_SAMPLE/hello-compute-buffer-vulkan-usl" \
  "$COMPUTE_SAMPLE/compute_buffer.us"
