#!/bin/zsh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/out/build/macos-debug"
FIXTURE="$BUILD_DIR/gpu-usl-fixture"
USLPACK="$BUILD_DIR/uslpack"
DEFAULT_ARTIFACT_DIR="$BUILD_DIR/usl/metal/samples"
SMOKE_ARTIFACT_DIR="$BUILD_DIR/usl/metal/smoke"
TRIANGLE_MANUAL_BIN="$BUILD_DIR/samples/gpu-triangle-manual/gpu-triangle-manual"
TRIANGLE_USL_BIN="$BUILD_DIR/samples/gpu-triangle-metal-usl/gpu-triangle-metal-usl"
TEXTURED_QUAD_BIN="$BUILD_DIR/samples/gpu-textured-quad-metal-usl/gpu-textured-quad-metal-usl"
MESH_TRIANGLE_BIN="$BUILD_DIR/samples/gpu-mesh-triangle-metal-usl/gpu-mesh-triangle-metal-usl"
COMPUTE_USL_BIN="$BUILD_DIR/samples/gpu-compute-metal-usl/gpu-compute-metal-usl"
COMPUTE_BUFFER_BIN="$BUILD_DIR/samples/gpu-compute-buffer-metal-usl/gpu-compute-buffer-metal-usl"

run_step() {
  local name="$1"
  shift

  echo "==> $name"
  "$@"
}

run_expect_fail_with_output() {
  local name="$1"
  local expected="$2"
  shift 2

  echo "==> $name"
  local output
  if output="$("$@" 2>&1)"; then
    echo "$output"
    echo "expected failure but command succeeded: $name" >&2
    return 1
  fi

  echo "$output"
  if ! printf "%s\n" "$output" | grep -Fq "$expected"; then
    echo "expected output not found for $name: $expected" >&2
    return 1
  fi
}

run_expect_output() {
  local name="$1"
  local expected="$2"
  shift 2

  echo "==> $name"
  local output
  if ! output="$("$@" 2>&1)"; then
    echo "$output"
    return 1
  fi

  echo "$output"
  if ! printf "%s\n" "$output" | grep -Fq "$expected"; then
    echo "expected output not found for $name: $expected" >&2
    return 1
  fi
}

run_binary() {
  local target="$1"
  shift

  (
    cd "$BUILD_DIR/samples/$target"
    "./$target" "$@"
  )
}

generate_artifact() {
  local mode="$1"
  local source="$2"
  local name="${source:t:r}"
  local outputDir="$SMOKE_ARTIFACT_DIR/$mode"
  local fixtureSource="$outputDir/$name.usl"

  mkdir -p "$outputDir"
  rm -f "$outputDir/$name.us" "$outputDir/$name.usl.metal"
  cp "$source" "$fixtureSource"

  case "$mode" in
    sidecar)
      env USL_EMIT_BYTECODE=1 \
          "$FIXTURE" metal "$fixtureSource"
      if [[ ! -f "$outputDir/$name.usl.metal" ]]; then
        echo "USL Metal sidecar was not generated: $name" >&2
        return 1
      fi
      ;;
    embedded)
      env USL_EMIT_BYTECODE=1 \
          USL_NO_BACKEND_SIDECAR=1 \
          "$FIXTURE" metal "$fixtureSource"
      if [[ -f "$outputDir/$name.usl.metal" ]]; then
        echo "unexpected USL Metal sidecar in embedded mode: $name" >&2
        return 1
      fi

      local platformMajor="$(sw_vers -productVersion | cut -d. -f1)"
      local mask
      for mask in {0..7}; do
        local -a packArgs=(
          --target metal msl2.0
          --platform macos "$platformMajor"
        )
        if ((mask & 1)); then packArgs+=(--cap subgroup); fi
        if ((mask & 2)); then packArgs+=(--cap descriptor_indexing); fi
        if ((mask & 4)); then packArgs+=(--cap ray_query); fi
        "$USLPACK" "${packArgs[@]}" "$outputDir/$name.us"
      done
      ;;
    *)
      echo "unknown USL artifact mode: $mode" >&2
      return 2
      ;;
  esac

  if [[ ! -f "$outputDir/$name.us" ]]; then
    echo "USL bytecode artifact was not generated: $name" >&2
    return 1
  fi
}

install_artifact() {
  local source="$1"
  local target="$2"
  local name="$3"
  local targetDir="$BUILD_DIR/samples/$target"

  rm -f "$targetDir/$name.usl.metal"
  cp "$source" "$targetDir/$name.us"
}

restore_artifact() {
  local target="$1"
  local name="$2"

  install_artifact "$DEFAULT_ARTIFACT_DIR/$name.us" "$target" "$name"
}

run_step "configure Metal samples" \
  cmake -S "$ROOT" --preset macos-debug

run_step "build Metal samples" \
  cmake --build "$BUILD_DIR" --target \
    gpu-uslpack \
    gpu-triangle-manual \
    gpu-triangle-metal-usl \
    gpu-textured-quad-metal-usl \
    gpu-mesh-triangle-metal-usl \
    gpu-compute-metal-usl \
    gpu-compute-buffer-metal-usl \
    gpu-usl-reflection-check \
    gpu-api-test

run_step "triangle-manual one-frame" \
  env GPU_SAMPLE_EXIT_AFTER_FRAMES=1 \
      "$TRIANGLE_MANUAL_BIN"

run_step "triangle-usl sidecar" \
  generate_artifact sidecar "$ROOT/samples/triangle-usl/triangle.usl"
run_step "triangle-usl embedded" \
  generate_artifact embedded "$ROOT/samples/triangle-usl/triangle.usl"
install_artifact "$SMOKE_ARTIFACT_DIR/embedded/triangle.us" \
  gpu-triangle-metal-usl triangle
run_expect_output "triangle-usl embedded one-frame" \
  "GPU: USL embedded payload" \
  env GPU_SAMPLE_EXIT_AFTER_FRAMES=1 GPU_USL_LOG=1 \
      "$TRIANGLE_USL_BIN"
restore_artifact gpu-triangle-metal-usl triangle
run_step "triangle-usl warm-frame allocations" \
  env GPU_SAMPLE_EXIT_AFTER_FRAMES=96 GPU_SAMPLE_ASSERT_ZERO_ALLOC=1 \
      "$TRIANGLE_USL_BIN"

run_step "textured-quad-usl sidecar" \
  generate_artifact sidecar \
    "$ROOT/samples/textured-quad-usl/textured_quad.usl"
run_step "textured-quad-usl embedded" \
  generate_artifact embedded \
    "$ROOT/samples/textured-quad-usl/textured_quad.usl"
install_artifact \
  "$SMOKE_ARTIFACT_DIR/embedded/textured_quad.us" \
  gpu-textured-quad-metal-usl textured_quad
run_step "textured-quad-usl embedded one-frame" \
  env GPU_SAMPLE_EXIT_AFTER_FRAMES=1 \
      "$TEXTURED_QUAD_BIN"
restore_artifact gpu-textured-quad-metal-usl textured_quad
run_step "textured-quad-usl warm-frame allocations" \
  env GPU_SAMPLE_EXIT_AFTER_FRAMES=96 GPU_SAMPLE_ASSERT_ZERO_ALLOC=1 \
      "$TEXTURED_QUAD_BIN"

run_step "mesh-triangle-usl one-frame" \
  env GPU_SAMPLE_EXIT_AFTER_FRAMES=1 \
      "$MESH_TRIANGLE_BIN"
run_step "mesh-triangle-usl warm-frame allocations" \
  env GPU_SAMPLE_EXIT_AFTER_FRAMES=96 GPU_SAMPLE_ASSERT_ZERO_ALLOC=1 \
      "$MESH_TRIANGLE_BIN"

run_step "compute-usl sidecar" \
  generate_artifact sidecar "$ROOT/samples/compute-usl/compute_visible.usl"
run_step "compute-usl embedded" \
  generate_artifact embedded "$ROOT/samples/compute-usl/compute_visible.usl"
install_artifact \
  "$SMOKE_ARTIFACT_DIR/embedded/compute_visible.us" \
  gpu-compute-metal-usl compute_visible
run_step "compute-usl embedded readback" \
  env GPU_SAMPLE_EXIT_AFTER_FRAMES=1 \
      "$COMPUTE_USL_BIN"
restore_artifact gpu-compute-metal-usl compute_visible
run_step "compute-usl warm-frame allocations" \
  env GPU_SAMPLE_EXIT_AFTER_FRAMES=96 GPU_SAMPLE_ASSERT_ZERO_ALLOC=1 \
      "$COMPUTE_USL_BIN"

run_step "compute-buffer-usl sidecar" \
  generate_artifact sidecar \
    "$ROOT/samples/compute-buffer-usl/compute_buffer.usl"
run_step "compute-buffer-usl embedded" \
  generate_artifact embedded \
    "$ROOT/samples/compute-buffer-usl/compute_buffer.usl"
install_artifact \
  "$SMOKE_ARTIFACT_DIR/embedded/compute_buffer.us" \
  gpu-compute-buffer-metal-usl compute_buffer
run_step "compute-buffer-usl embedded readback" \
  env GPU_SAMPLE_EXIT_AFTER_FRAMES=1 \
      "$COMPUTE_BUFFER_BIN"
restore_artifact gpu-compute-buffer-metal-usl compute_buffer
run_step "compute-buffer-usl warm-frame allocations" \
  env GPU_SAMPLE_EXIT_AFTER_FRAMES=96 GPU_SAMPLE_ASSERT_ZERO_ALLOC=1 \
      "$COMPUTE_BUFFER_BIN"
run_expect_fail_with_output "compute-buffer-usl missing group 1 bind" \
  "GPU validation: GPUDispatchIndirect skipped: missing compute bind group" \
  env GPU_SAMPLE_EXIT_AFTER_FRAMES=1 \
      GPU_SAMPLE_VERBOSE_VALIDATION=1 \
      GPU_SAMPLE_SKIP_COMPUTE_BIND=1 \
      "$COMPUTE_BUFFER_BIN"

run_step "api validation" \
  ctest --test-dir "$BUILD_DIR" --output-on-failure -R '^api-validation$'

run_step "usl-reflection-check generated" \
  run_binary gpu-usl-reflection-check \
    "$DEFAULT_ARTIFACT_DIR/reflection.us" \
    "$DEFAULT_ARTIFACT_DIR/reflection_storage.us"
run_step "usl-reflection-check embedded reflection" \
  generate_artifact embedded \
    "$ROOT/samples/usl-reflection-check/reflection.usl"
run_step "usl-reflection-check embedded storage" \
  generate_artifact embedded \
    "$ROOT/samples/usl-reflection-check/reflection_storage.usl"
run_step "usl-reflection-check embedded" \
  run_binary gpu-usl-reflection-check \
    "$SMOKE_ARTIFACT_DIR/embedded/reflection.us" \
    "$SMOKE_ARTIFACT_DIR/embedded/reflection_storage.us"

echo "Smoke passed"
