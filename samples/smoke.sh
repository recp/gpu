#!/bin/zsh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

run_step() {
  local name="$1"
  shift

  echo "==> $name"
  "$@"
}

run_expect_fail() {
  local name="$1"
  shift

  echo "==> $name"
  if "$@"; then
    echo "expected failure but command succeeded: $name" >&2
    return 1
  fi
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

run_sample() {
  local name="$1"
  shift

  (
    cd "$ROOT/samples/$name"
    "$@"
  )
}

run_api_test() {
  (
    cd "$ROOT/tests/api"
    "$@"
  )
}

run_step "triangle-manual" \
  run_sample triangle-manual ./build.sh

run_step "triangle-manual one-frame" \
  run_sample triangle-manual env GPU_SAMPLE_EXIT_AFTER_FRAMES=1 ./hello-triangle-manual

run_step "triangle-usl sidecar" \
  run_sample triangle-usl ./build.sh

run_step "triangle-usl embedded no-sidecar" \
  run_sample triangle-usl env GPU_USL_EMBED_METAL=1 GPU_USL_NO_SIDECAR=1 ./build.sh

run_step "triangle-usl one-frame" \
  run_sample triangle-usl env GPU_SAMPLE_EXIT_AFTER_FRAMES=1 GPU_USL_NO_SIDECAR=1 ./hello-triangle-usl

run_step "triangle-usl warm-frame allocations" \
  run_sample triangle-usl env GPU_SAMPLE_EXIT_AFTER_FRAMES=96 GPU_SAMPLE_ASSERT_ZERO_ALLOC=1 GPU_USL_NO_SIDECAR=1 ./hello-triangle-usl

run_step "textured-quad-usl sidecar" \
  run_sample textured-quad-usl ./build.sh

run_step "textured-quad-usl embedded no-sidecar" \
  run_sample textured-quad-usl env GPU_USL_EMBED_METAL=1 GPU_USL_NO_SIDECAR=1 ./build.sh

run_step "textured-quad-usl one-frame" \
  run_sample textured-quad-usl env GPU_SAMPLE_EXIT_AFTER_FRAMES=1 GPU_USL_NO_SIDECAR=1 ./hello-textured-quad-usl

run_step "textured-quad-usl warm-frame allocations" \
  run_sample textured-quad-usl env GPU_SAMPLE_EXIT_AFTER_FRAMES=96 GPU_SAMPLE_ASSERT_ZERO_ALLOC=1 GPU_USL_NO_SIDECAR=1 ./hello-textured-quad-usl

run_step "compute-usl sidecar" \
  run_sample compute-usl ./build.sh

run_step "compute-usl embedded no-sidecar" \
  run_sample compute-usl env GPU_USL_EMBED_METAL=1 GPU_USL_NO_SIDECAR=1 ./build.sh

run_step "compute-usl readback" \
  run_sample compute-usl env GPU_SAMPLE_EXIT_AFTER_FRAMES=1 GPU_USL_NO_SIDECAR=1 ./hello-compute-usl

run_step "compute-buffer-usl sidecar" \
  run_sample compute-buffer-usl ./build.sh

run_step "compute-buffer-usl generated no-sidecar" \
  run_sample compute-buffer-usl env GPU_USL_NO_SIDECAR=1 ./build.sh

run_step "compute-buffer-usl readback" \
  run_sample compute-buffer-usl env GPU_SAMPLE_EXIT_AFTER_FRAMES=1 GPU_USL_NO_SIDECAR=1 ./hello-compute-buffer-usl

run_step "compute-buffer-usl warm-frame allocations" \
  run_sample compute-buffer-usl env GPU_SAMPLE_EXIT_AFTER_FRAMES=96 GPU_SAMPLE_ASSERT_ZERO_ALLOC=1 GPU_USL_NO_SIDECAR=1 ./hello-compute-buffer-usl

run_expect_fail_with_output "compute-buffer-usl missing group 1 bind" \
  "GPU validation: GPUDispatchIndirect skipped: missing compute bind group" \
  run_sample compute-buffer-usl env GPU_SAMPLE_EXIT_AFTER_FRAMES=1 GPU_SAMPLE_VERBOSE_VALIDATION=1 GPU_SAMPLE_SKIP_COMPUTE_BIND=1 ./hello-compute-buffer-usl

run_step "compute-buffer-usl embedded no-sidecar" \
  run_sample compute-buffer-usl env GPU_USL_EMBED_METAL=1 GPU_USL_NO_SIDECAR=1 ./build.sh

run_step "api validation" \
  run_api_test ./build.sh

run_step "usl-reflection-check generated" \
  run_sample usl-reflection-check ./build.sh

run_step "usl-reflection-check embedded" \
  run_sample usl-reflection-check env GPU_USL_EMBED_METAL=1 ./build.sh

echo "Smoke passed"
