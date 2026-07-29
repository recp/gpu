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
      echo "samples: select apple, xlib, or wayland" >&2
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
    desktop="$HOME/Desktop"
    mkdir -p "$desktop"
    ln -sfn "$app" "$desktop/GPU + USL Samples.app"
    ;;
  xlib|wayland)
    gallery="$build_dir/samples/linux/$platform/gpu-gallery-$platform"
    [ -x "$gallery" ] || {
      echo "samples: build $platform first" >&2
      exit 1
    }
    desktop=${XDG_DESKTOP_DIR:-"$HOME/Desktop"}
    mkdir -p "$desktop"
    rm -f "$desktop/GPU + USL ${platform}.desktop"
    shortcut="$desktop/gpu-usl-$platform.desktop"
    cat >"$shortcut" <<EOF
[Desktop Entry]
Type=Application
Name=GPU + USL ${platform}
Comment=Portable GPU and USL samples
Exec=${gallery}
Terminal=false
Categories=Development;Graphics;
EOF
    chmod +x "$shortcut"
    runtime_dir=${XDG_RUNTIME_DIR:-"/run/user/$(id -u)"}
    if [ -S "$runtime_dir/bus" ]; then
      XDG_RUNTIME_DIR=$runtime_dir \
      DBUS_SESSION_BUS_ADDRESS="unix:path=$runtime_dir/bus" \
        gio set -t string "$shortcut" metadata::trusted true 2>/dev/null ||
        true
    fi
    ;;
  *)
    echo "samples: shortcut is not available for $platform" >&2
    exit 1
    ;;
esac

echo "samples: installed $platform desktop shortcut"
