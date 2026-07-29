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

case "$platform" in
  apple)
    app="$build_dir/samples/gpu-gallery-apple/GPU + USL Samples.app"
    [ -d "$app" ] || {
      echo "samples: build apple first" >&2
      exit 1
    }
    open "$app"
    ;;
  web)
    root="$build_dir/samples/webgpu"
    [ -f "$root/index.html" ] || {
      echo "samples: build web first" >&2
      exit 1
    }
    exec "$sample_dir/shell/web/serve.sh" "$root"
    ;;
  android)
    apk="$build_dir/samples/android/gpu-android-gallery/gpu-android-gallery.apk"
    [ -f "$apk" ] || {
      echo "samples: build android first" >&2
      exit 1
    }
    android_sdk=${GPU_ANDROID_SDK_ROOT:-${ANDROID_SDK_ROOT:-${ANDROID_HOME:-}}}
    if [ -z "$android_sdk" ] && [ -d "$HOME/Library/Developer/Android/sdk" ]; then
      android_sdk="$HOME/Library/Developer/Android/sdk"
    fi
    adb=${GPU_ADB:-"$android_sdk/platform-tools/adb"}
    [ -x "$adb" ] || {
      echo "samples: adb not found" >&2
      exit 1
    }
    "$adb" install -r "$apk"
    "$adb" shell am force-stop gpu.samples
    "$adb" shell am start -n gpu.samples/.GalleryActivity
    ;;
  xlib|wayland)
    gallery="$build_dir/samples/linux/$platform/gpu-gallery-$platform"
    [ -x "$gallery" ] || {
      echo "samples: build $platform first" >&2
      exit 1
    }
    exec "$gallery"
    ;;
  *)
    echo "samples: unknown platform: $platform" >&2
    exit 1
    ;;
esac
