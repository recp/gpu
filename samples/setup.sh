#!/bin/sh

set -eu

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

run_root() {
  if [ "$(id -u)" -eq 0 ]; then
    "$@"
  elif command -v sudo >/dev/null 2>&1; then
    sudo "$@"
  else
    echo "samples: sudo is required to install system packages" >&2
    exit 1
  fi
}

install_brew() {
  command -v brew >/dev/null 2>&1 || {
    echo "samples: install Homebrew first: https://brew.sh" >&2
    exit 1
  }
  brew install "$@"
}

install_linux() {
  window_system=$1

  if command -v apt-get >/dev/null 2>&1; then
    packages="cmake ninja-build pkg-config libvulkan-dev vulkan-tools \
vulkan-validationlayers libcglm-dev libcurl4-openssl-dev libgtk-3-dev \
libpng-dev libjpeg-dev"
    if [ "$window_system" = xlib ]; then
      packages="$packages libx11-dev"
    else
      packages="$packages libwayland-dev wayland-protocols libxkbcommon-dev \
libdecor-0-dev"
    fi
    run_root apt-get update
    # Package names are intentionally kept visible for reproducible setup.
    # shellcheck disable=SC2086
    run_root apt-get install -y $packages
    return
  fi

  if command -v dnf >/dev/null 2>&1; then
    packages="cmake ninja-build pkgconf-pkg-config vulkan-loader-devel \
vulkan-headers vulkan-tools vulkan-validation-layers-devel cglm-devel \
libcurl-devel gtk3-devel libpng-devel"
    packages="$packages libjpeg-turbo-devel"
    if [ "$window_system" = xlib ]; then
      packages="$packages libX11-devel"
    else
      packages="$packages wayland-devel wayland-protocols-devel \
libxkbcommon-devel libdecor-devel"
    fi
    # shellcheck disable=SC2086
    run_root dnf install -y $packages
    return
  fi

  if command -v pacman >/dev/null 2>&1; then
    packages="cmake ninja pkgconf vulkan-headers vulkan-loader vulkan-tools \
vulkan-validation-layers cglm curl gtk3 libpng"
    packages="$packages libjpeg-turbo"
    if [ "$window_system" = xlib ]; then
      packages="$packages libx11"
    else
      packages="$packages wayland wayland-protocols libxkbcommon libdecor"
    fi
    # shellcheck disable=SC2086
    run_root pacman -S --needed $packages
    return
  fi

  echo "samples: supported Linux package managers are apt, dnf, and pacman" >&2
  exit 1
}

case "$platform" in
  apple)
    [ "$(uname -s)" = Darwin ] || {
      echo "samples: apple setup requires macOS" >&2
      exit 1
    }
    install_brew cmake ninja cglm
    xcode-select -p >/dev/null 2>&1 || {
      echo "samples: run xcode-select --install" >&2
      exit 1
    }
    ;;
  web)
    [ "$(uname -s)" = Darwin ] || {
      echo "samples: automatic web setup currently supports macOS" >&2
      exit 1
    }
    install_brew cmake ninja emscripten caddy mkcert
    ;;
  android)
    case $(uname -s) in
      Darwin) install_brew cmake ninja ;;
      Linux)  install_linux xlib ;;
      *)
        echo "samples: automatic Android host setup supports macOS and Linux" >&2
        exit 1
        ;;
    esac
    echo "samples: install Android SDK platform 30, NDK, and platform-tools"
    echo "samples: accept SDK licenses before running samples/build.sh android"
    ;;
  xlib|wayland)
    [ "$(uname -s)" = Linux ] || {
      echo "samples: $platform setup requires Linux" >&2
      exit 1
    }
    install_linux "$platform"
    ;;
  *)
    echo "samples: unknown platform: $platform" >&2
    exit 1
    ;;
esac

echo "samples: $platform dependencies are ready"
