#!/bin/zsh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEST_DIR="$(cd "$(dirname "$0")" && pwd)"
OUT_BIN="$TEST_DIR/api-tests"
SDK_PATH="$(xcrun --sdk macosx --show-sdk-path)"
US_ROOT="${USL_ROOT:-/Users/recp/Projects/recp/UniversalShading/us}"
USTEST="${USL_USTEST:-$US_ROOT/build/ustest}"
FIXTURE_DIR="${GPU_API_USL_FIXTURE_DIR:-/tmp/gpu-api-usl-fixtures}"
EMBED_METAL="${GPU_USL_EMBED_METAL:-0}"

if [[ ! -x "$USTEST" ]]; then
  echo "ustest not found at $USTEST" >&2
  exit 1
fi

rm -rf "$FIXTURE_DIR"
mkdir -p "$FIXTURE_DIR"
cp "$ROOT/samples/usl-reflection-check/reflection.usl" "$FIXTURE_DIR/reflection.usl"
cp "$TEST_DIR/render_mrt.usl" "$FIXTURE_DIR/render_mrt.usl"
cp "$TEST_DIR/compute.usl" "$FIXTURE_DIR/compute.usl"
cp "$TEST_DIR/source_sampler.usl" "$FIXTURE_DIR/source_sampler.usl"
cp "$TEST_DIR/storage_texture.usl" "$FIXTURE_DIR/storage_texture.usl"

for shader in reflection render_mrt compute source_sampler storage_texture; do
  ustest_cmd=("$USTEST" --shader "$FIXTURE_DIR/$shader.usl" --no-logs --no-sidecar)
  if [[ "$EMBED_METAL" == "1" ]]; then
    USL_EMBED_METAL_BLOB=1 "${ustest_cmd[@]}" \
      >"/tmp/gpu-api-tests-$shader-ustest.log" 2>&1
  else
    "${ustest_cmd[@]}" >"/tmp/gpu-api-tests-$shader-ustest.log" 2>&1
  fi

  if [[ ! -f "$FIXTURE_DIR/$shader.us" ]]; then
    echo "USL bytecode artifact was not generated: $FIXTURE_DIR/$shader.us" >&2
    exit 1
  fi
  if [[ -f "$FIXTURE_DIR/$shader.usl.metal" ]]; then
    echo "USL Metal sidecar should not be generated in --no-sidecar mode" >&2
    exit 1
  fi
done

source "$ROOT/samples/common/build-library.sh"
gpu_build_library "$ROOT" metal

xcrun --sdk macosx clang \
  -std=c11 \
  -Wall \
  -Wextra \
  -Werror \
  -isysroot "$SDK_PATH" \
  -I"$ROOT/include" \
  "$TEST_DIR"/*.c \
  -L"$LIB_DIR" \
  -lgpu \
  -Wl,-rpath,"$LIB_DIR" \
  -o "$OUT_BIN"

"$OUT_BIN" \
  "$FIXTURE_DIR/reflection.us" \
  "$FIXTURE_DIR/render_mrt.us" \
  "$FIXTURE_DIR/compute.us" \
  "$FIXTURE_DIR/source_sampler.us" \
  "$FIXTURE_DIR/storage_texture.us"
