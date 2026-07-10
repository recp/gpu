#!/bin/zsh

gpu_build_library() {
  local root="$1"
  local backend="${2:-metal}"
  local preset

  case "$backend" in
    metal)
      preset="macos-debug"
      ;;
    vulkan)
      preset="macos-vulkan-debug"
      export VULKAN_SDK="${VULKAN_SDK:-$HOME/Library/Developer/VulkanSDK/1.4.350.1}"
      ;;
    *)
      echo "unknown GPU backend: $backend" >&2
      return 1
      ;;
  esac

  (cd "$root" && cmake --preset "$preset")
  (cd "$root" && cmake --build --preset "$preset" --target gpu)

  LIB_DIR="$root/out/build/$preset"
  US_LIB_DIR="$LIB_DIR/_deps/us"
  US_DS_LIB_DIR="$US_LIB_DIR/deps/ds"
  export LIB_DIR US_LIB_DIR US_DS_LIB_DIR
}
