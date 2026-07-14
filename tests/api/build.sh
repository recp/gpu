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
BACKEND="${GPU_API_BACKEND:-metal}"
VULKAN_SDK="${VULKAN_SDK:-$HOME/Library/Developer/VulkanSDK/1.4.350.1}"

case "$BACKEND" in
  metal|vulkan) ;;
  *)
    echo "unsupported GPU API test backend: $BACKEND" >&2
    exit 2
    ;;
esac

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
cp "$TEST_DIR/cube_texture.usl" "$FIXTURE_DIR/cube_texture.usl"
cp "$TEST_DIR/line_texture.usl" "$FIXTURE_DIR/line_texture.usl"
cp "$TEST_DIR/volume_texture.usl" "$FIXTURE_DIR/volume_texture.usl"
cp "$TEST_DIR/descriptor_arrays.usl" "$FIXTURE_DIR/descriptor_arrays.usl"
cp "$TEST_DIR/descriptor_indexing.usl" "$FIXTURE_DIR/descriptor_indexing.usl"
cp "$TEST_DIR/subgroup.usl" "$FIXTURE_DIR/subgroup.usl"
cp "$TEST_DIR/shader_f16.usl" "$FIXTURE_DIR/shader_f16.usl"

shaders=(
  reflection
  render_mrt
  compute
  source_sampler
  storage_texture
  cube_texture
  line_texture
  volume_texture
  descriptor_arrays
  descriptor_indexing
  subgroup
  shader_f16
)
for shader in "${shaders[@]}"; do
  target_env=()
  case "$shader" in
    subgroup)   target_env=(USL_TARGET_CAPS=subgroup) ;;
    shader_f16) target_env=(USL_TARGET_CAPS=vulkan1_2,shader_f16) ;;
    descriptor_indexing) target_env=(USL_TARGET_CAPS=descriptor_indexing) ;;
  esac
  ustest_cmd=(
    env "${target_env[@]}"
    "$USTEST"
    --shader "$FIXTURE_DIR/$shader.usl"
    --no-logs
    --no-sidecar
  )
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
gpu_build_library "$ROOT" "$BACKEND"

link_args=(
  -Wl,-rpath,"$LIB_DIR"
  -Wl,-rpath,"$US_LIB_DIR"
  -Wl,-rpath,"$US_DS_LIB_DIR"
)
if [[ "$BACKEND" == "vulkan" ]]; then
  link_args+=(-Wl,-rpath,"$VULKAN_SDK/macOS/lib")
fi

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
  "${link_args[@]}" \
  -o "$OUT_BIN"

if [[ "$BACKEND" == "vulkan" ]]; then
  export VK_ICD_FILENAMES="$VULKAN_SDK/macOS/share/vulkan/icd.d/MoltenVK_icd.json"
fi

"$OUT_BIN" \
  "$FIXTURE_DIR/reflection.us" \
  "$FIXTURE_DIR/render_mrt.us" \
  "$FIXTURE_DIR/compute.us" \
  "$FIXTURE_DIR/source_sampler.us" \
  "$FIXTURE_DIR/storage_texture.us" \
  "$FIXTURE_DIR/cube_texture.us" \
  "$FIXTURE_DIR/line_texture.us" \
  "$FIXTURE_DIR/volume_texture.us" \
  "$FIXTURE_DIR/descriptor_arrays.us" \
  "$FIXTURE_DIR/descriptor_indexing.us" \
  "$FIXTURE_DIR/subgroup.us" \
  "$FIXTURE_DIR/shader_f16.us" \
  "$BACKEND"
