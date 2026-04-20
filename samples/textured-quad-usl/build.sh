#!/bin/zsh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SAMPLE_DIR="$(cd "$(dirname "$0")" && pwd)"
DERIVED_DATA="/tmp/gpu-dd"
LIB_DIR="$DERIVED_DATA/Build/Products/Debug"
OUT_BIN="$SAMPLE_DIR/hello-textured-quad-usl"
SDK_PATH="$(xcrun --sdk macosx --show-sdk-path)"
US_ROOT="${USL_ROOT:-/Users/recp/Projects/recp/UniversalShading/us}"
USTEST="${USL_USTEST:-$US_ROOT/build/ustest}"
US_LIB_DIR="$US_ROOT/build/us"
US_DS_LIB_DIR="$US_LIB_DIR/deps/ds"
EMBED_METAL="${GPU_USL_EMBED_METAL:-0}"
NO_SIDECAR="${GPU_USL_NO_SIDECAR:-0}"

if [[ ! -x "$USTEST" ]]; then
  echo "ustest not found at $USTEST" >&2
  exit 1
fi

rm -f "$SAMPLE_DIR/textured_quad.usl.metal"

ustest_cmd=("$USTEST" --shader "$SAMPLE_DIR/textured_quad.usl" --no-logs)
if [[ "$NO_SIDECAR" == "1" ]]; then
  ustest_cmd+=(--no-sidecar)
fi

if [[ "$EMBED_METAL" == "1" ]]; then
  USL_EMBED_METAL_BLOB=1 "${ustest_cmd[@]}" >/tmp/gpu-textured-quad-usl-ustest.log 2>&1
else
  "${ustest_cmd[@]}" >/tmp/gpu-textured-quad-usl-ustest.log 2>&1
fi

if [[ ! -f "$SAMPLE_DIR/textured_quad.us" ]]; then
  echo "USL bytecode artifact was not generated: $SAMPLE_DIR/textured_quad.us" >&2
  exit 1
fi

if [[ "$NO_SIDECAR" == "1" ]]; then
  if [[ -f "$SAMPLE_DIR/textured_quad.usl.metal" ]]; then
    echo "USL Metal sidecar should not be generated in --no-sidecar mode" >&2
    exit 1
  fi
else
  if [[ ! -f "$SAMPLE_DIR/textured_quad.usl.metal" ]]; then
    echo "USL Metal sidecar was not generated: $SAMPLE_DIR/textured_quad.usl.metal" >&2
    exit 1
  fi
fi

xcodebuild \
  -project "$ROOT/gpu.xcodeproj" \
  -scheme gpu \
  -configuration Debug \
  -derivedDataPath "$DERIVED_DATA" \
  CODE_SIGNING_ALLOWED=NO \
  CODE_SIGNING_REQUIRED=NO \
  build

xcrun --sdk macosx clang \
  -fobjc-arc \
  -isysroot "$SDK_PATH" \
  -I"$ROOT/include" \
  -framework AppKit \
  -framework Metal \
  -framework Foundation \
  -framework QuartzCore \
  "$SAMPLE_DIR/main.m" \
  -L"$LIB_DIR" \
  -lgpu \
  -Wl,-rpath,"$LIB_DIR" \
  -Wl,-rpath,"$US_LIB_DIR" \
  -Wl,-rpath,"$US_DS_LIB_DIR" \
  -o "$OUT_BIN"

echo "Built $OUT_BIN"
