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
    -L"$LIB_DIR" \
    -L"$VULKAN_SDK/macOS/lib" \
    -lgpu \
    -lvulkan \
    -Wl,-rpath,"$LIB_DIR" \
    -Wl,-rpath,"$US_LIB_DIR" \
    -Wl,-rpath,"$VULKAN_SDK/macOS/lib" \
    -o "$output"
}

build_test "$TEST_DIR/queue.c" "$TEST_DIR/vulkan-queue"
build_test "$TEST_DIR/shader.c" "$TEST_DIR/vulkan-shader"

COMPUTE_SAMPLE="$ROOT/samples/compute-buffer-vulkan-usl"
"$USTEST" --shader "$COMPUTE_SAMPLE/compute_buffer.usl" \
  --no-logs --no-sidecar >/tmp/gpu-vulkan-compute-ustest.log 2>&1
build_test "$COMPUTE_SAMPLE/main.c" \
  "$COMPUTE_SAMPLE/hello-compute-buffer-vulkan-usl"

VK_ICD_FILENAMES="$ICD" "$TEST_DIR/vulkan-queue"
VK_ICD_FILENAMES="$ICD" \
  "$TEST_DIR/vulkan-shader" \
  "$ROOT/samples/triangle-usl/triangle.us"
VK_ICD_FILENAMES="$ICD" \
  "$COMPUTE_SAMPLE/hello-compute-buffer-vulkan-usl" \
  "$COMPUTE_SAMPLE/compute_buffer.us"
