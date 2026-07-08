#!/bin/zsh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DERIVED_DATA="${GPU_DERIVED_DATA:-/tmp/gpu-dd}"

run_step() {
  local name="$1"
  shift

  echo "==> $name"
  "$@"
}

run_sample() {
  local name="$1"
  shift

  (
    cd "$ROOT/samples/$name"
    "$@"
  )
}

run_step "triangle-manual" \
  run_sample triangle-manual env GPU_DERIVED_DATA="$DERIVED_DATA" ./build.sh

run_step "triangle-usl sidecar" \
  run_sample triangle-usl env GPU_DERIVED_DATA="$DERIVED_DATA" ./build.sh

run_step "triangle-usl embedded no-sidecar" \
  run_sample triangle-usl env GPU_DERIVED_DATA="$DERIVED_DATA" GPU_USL_EMBED_METAL=1 GPU_USL_NO_SIDECAR=1 ./build.sh

run_step "textured-quad-usl sidecar" \
  run_sample textured-quad-usl env GPU_DERIVED_DATA="$DERIVED_DATA" ./build.sh

run_step "textured-quad-usl embedded no-sidecar" \
  run_sample textured-quad-usl env GPU_DERIVED_DATA="$DERIVED_DATA" GPU_USL_EMBED_METAL=1 GPU_USL_NO_SIDECAR=1 ./build.sh

run_step "compute-usl sidecar" \
  run_sample compute-usl env GPU_DERIVED_DATA="$DERIVED_DATA" ./build.sh

run_step "compute-usl embedded no-sidecar" \
  run_sample compute-usl env GPU_DERIVED_DATA="$DERIVED_DATA" GPU_USL_EMBED_METAL=1 GPU_USL_NO_SIDECAR=1 ./build.sh

run_step "compute-buffer-usl sidecar" \
  run_sample compute-buffer-usl env GPU_DERIVED_DATA="$DERIVED_DATA" ./build.sh

run_step "compute-buffer-usl generated no-sidecar" \
  run_sample compute-buffer-usl env GPU_DERIVED_DATA="$DERIVED_DATA" GPU_USL_NO_SIDECAR=1 ./build.sh

run_step "compute-buffer-usl readback" \
  run_sample compute-buffer-usl env GPU_DERIVED_DATA="$DERIVED_DATA" GPU_SAMPLE_EXIT_AFTER_FRAMES=1 GPU_USL_NO_SIDECAR=1 ./hello-compute-buffer-usl

run_step "compute-buffer-usl embedded no-sidecar" \
  run_sample compute-buffer-usl env GPU_DERIVED_DATA="$DERIVED_DATA" GPU_USL_EMBED_METAL=1 GPU_USL_NO_SIDECAR=1 ./build.sh

run_step "usl-reflection-check generated" \
  run_sample usl-reflection-check env GPU_DERIVED_DATA="$DERIVED_DATA" ./build.sh

run_step "usl-reflection-check embedded" \
  run_sample usl-reflection-check env GPU_DERIVED_DATA="$DERIVED_DATA" GPU_USL_EMBED_METAL=1 ./build.sh

echo "Smoke passed"
