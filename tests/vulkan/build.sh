#!/bin/zsh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEST_DIR="$(cd "$(dirname "$0")" && pwd)"
DERIVED_DATA="${GPU_VULKAN_DERIVED_DATA:-/tmp/gpu-vk-dd}"
VULKAN_SDK="${VULKAN_SDK:-$HOME/Library/Developer/VulkanSDK/1.4.350.1}"
LIB_DIR="$DERIVED_DATA/Build/Products/Debug"
US_LIB_DIR="${USL_ROOT:-/Users/recp/Projects/recp/UniversalShading/us}/build/us"
ICD="$VULKAN_SDK/macOS/share/vulkan/icd.d/MoltenVK_icd.json"
SDK_PATH="$(xcrun --sdk macosx --show-sdk-path)"

xcodebuild \
  -project "$ROOT/gpu.xcodeproj" \
  -scheme gpu \
  -configuration Debug \
  -derivedDataPath "$DERIVED_DATA" \
  CODE_SIGNING_ALLOWED=NO \
  CODE_SIGNING_REQUIRED=NO \
  'GCC_PREPROCESSOR_DEFINITIONS=$(inherited) GPU_ENABLE_VULKAN=1' \
  'OTHER_LDFLAGS=$(inherited) -lvulkan' \
  build

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
    "$source" \
    -L"$LIB_DIR" \
    -lgpu \
    -Wl,-rpath,"$LIB_DIR" \
    -Wl,-rpath,"$US_LIB_DIR" \
    -Wl,-rpath,"$VULKAN_SDK/macOS/lib" \
    -o "$output"
}

build_test "$TEST_DIR/queue.c" "$TEST_DIR/vulkan-queue"
build_test "$TEST_DIR/shader.c" "$TEST_DIR/vulkan-shader"

VK_ICD_FILENAMES="$ICD" "$TEST_DIR/vulkan-queue"
VK_ICD_FILENAMES="$ICD" \
  "$TEST_DIR/vulkan-shader" \
  "$ROOT/samples/triangle-usl/triangle.us"
