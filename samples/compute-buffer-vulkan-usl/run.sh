#!/bin/zsh
set -euo pipefail

SAMPLE_DIR="$(cd "$(dirname "$0")" && pwd)"
VULKAN_SDK="${VULKAN_SDK:-$HOME/Library/Developer/VulkanSDK/1.4.350.1}"

cd "$SAMPLE_DIR"
VK_ICD_FILENAMES="$VULKAN_SDK/macOS/share/vulkan/icd.d/MoltenVK_icd.json" \
  ./hello-compute-buffer-vulkan-usl "$SAMPLE_DIR/compute_buffer.us"
