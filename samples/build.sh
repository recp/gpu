#!/bin/sh

set -eu

sample_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root_dir=$(dirname "$sample_dir")
platform=${1:-}

if [ -z "$platform" ]; then
  case $(uname -s) in
    Darwin) platform=apple ;;
    Linux)  platform=xlib ;;
    *)
      echo "samples: select apple, web, android, xlib, or wayland" >&2
      exit 1
      ;;
  esac
fi

build_dir=${GPU_SAMPLE_BUILD_DIR:-"$root_dir/out/build/samples-$platform"}
build_type=${GPU_SAMPLE_BUILD_TYPE:-Release}
usl_root=${GPU_USL_ROOT:-${USL_ROOT:-"$root_dir/../UniversalShading/us"}}
assetkit_root=${GPU_ASSETKIT_ROOT:-"$root_dir/../assetio"}

host_fixture=${GPU_USL_HOST_FIXTURE:-"$root_dir/out/build/release-check/gpu-usl-fixture"}
host_packer=${GPU_USL_HOST_PACKER:-"$root_dir/out/build/release-check/uslpack"}

require_host_tools() {
  host_build="$root_dir/out/build/release-check"
  if [ "$host_fixture" = "$host_build/gpu-usl-fixture" ] &&
     [ "$host_packer" = "$host_build/uslpack" ] &&
     [ -f "$host_build/CMakeCache.txt" ]; then
    cmake --build "$host_build" --target gpu-usl-fixture uslpack
  fi
  if [ ! -x "$host_fixture" ] || [ ! -x "$host_packer" ]; then
    echo "samples: build the host shader tools first:" >&2
    echo "  cmake --build $root_dir/out/build/release-check --target gpu-usl-fixture uslpack" >&2
    exit 1
  fi
}

case "$platform" in
  apple)
    [ "$(uname -s)" = Darwin ] || {
      echo "samples: apple requires macOS" >&2
      exit 1
    }
    cmake -S "$root_dir" -B "$build_dir" -G Ninja \
      -DCMAKE_BUILD_TYPE="$build_type" \
      -DBUILD_SHARED_LIBS=OFF \
      -DGPU_BACKEND_METAL_ONLY=ON \
      -DGPU_USL_ROOT="$usl_root" \
      -DGPU_ASSETKIT_ROOT="$assetkit_root" \
      -DGPU_BUILD_SAMPLES=ON
    cmake --build "$build_dir" --target gpu-gallery-apple
    ;;
  web)
    require_host_tools
    command -v emcmake >/dev/null 2>&1 || {
      echo "samples: emcmake is required for web" >&2
      exit 1
    }
    emcmake cmake -S "$root_dir" -B "$build_dir" -G Ninja \
      -DCMAKE_BUILD_TYPE="$build_type" \
      -DBUILD_SHARED_LIBS=OFF \
      -DGPU_BACKEND_WEBGPU_ONLY=ON \
      -DGPU_USL_ROOT="$usl_root" \
      -DGPU_ASSETKIT_ROOT="$assetkit_root" \
      -DGPU_BUILD_SAMPLES=ON \
      -DGPU_USL_HOST_FIXTURE="$host_fixture" \
      -DGPU_USL_HOST_PACKER="$host_packer"
    cmake --build "$build_dir"
    ;;
  android)
    require_host_tools
    android_sdk=${GPU_ANDROID_SDK_ROOT:-${ANDROID_SDK_ROOT:-${ANDROID_HOME:-}}}
    if [ -z "$android_sdk" ] && [ -d "$HOME/Library/Developer/Android/sdk" ]; then
      android_sdk="$HOME/Library/Developer/Android/sdk"
    fi
    [ -d "$android_sdk" ] || {
      echo "samples: set ANDROID_SDK_ROOT" >&2
      exit 1
    }
    ndk_dir=${GPU_ANDROID_NDK:-}
    if [ -z "$ndk_dir" ]; then
      ndk_dir=$(find "$android_sdk/ndk" -mindepth 1 -maxdepth 1 \
        -type d 2>/dev/null | sort | tail -1)
    fi
    [ -f "$ndk_dir/build/cmake/android.toolchain.cmake" ] || {
      echo "samples: Android NDK not found" >&2
      exit 1
    }
    cmake -S "$root_dir" -B "$build_dir" -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE="$ndk_dir/build/cmake/android.toolchain.cmake" \
      -DANDROID_ABI="${GPU_ANDROID_ABI:-arm64-v8a}" \
      -DANDROID_PLATFORM="${GPU_ANDROID_PLATFORM:-android-30}" \
      -DCMAKE_BUILD_TYPE="$build_type" \
      -DBUILD_SHARED_LIBS=OFF \
      -DGPU_BACKEND_VULKAN_ONLY=ON \
      -DGPU_USL_ROOT="$usl_root" \
      -DGPU_ASSETKIT_ROOT="$assetkit_root" \
      -DGPU_BUILD_ANDROID_SAMPLES=ON \
      -DGPU_USL_HOST_FIXTURE="$host_fixture" \
      -DGPU_USL_HOST_PACKER="$host_packer" \
      -DGPU_ANDROID_SDK_ROOT="$android_sdk"
    cmake --build "$build_dir" --target gpu-android-gallery-apk
    ;;
  xlib|wayland)
    [ "$(uname -s)" = Linux ] || {
      echo "samples: $platform requires Linux" >&2
      exit 1
    }
    cmake -S "$root_dir" -B "$build_dir" -G Ninja \
      -DCMAKE_BUILD_TYPE="$build_type" \
      -DBUILD_SHARED_LIBS=OFF \
      -DGPU_BACKEND_VULKAN_ONLY=ON \
      -DGPU_USL_ROOT="$usl_root" \
      -DGPU_ASSETKIT_ROOT="$assetkit_root" \
      -DGPU_BUILD_SAMPLES=ON
    cmake --build "$build_dir" --target "gpu-gallery-$platform"
    ;;
  *)
    echo "samples: unknown platform: $platform" >&2
    exit 1
    ;;
esac

echo "samples: built $platform in $build_dir"
