#!/bin/zsh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/out/build/macos-debug"
SAMPLE_BIN="$BUILD_DIR/samples/gpu-triangle-manual/gpu-triangle-manual"
TIMEOUT_SECONDS="${GPU_LIFECYCLE_TIMEOUT_SECONDS:-10}"

cmake --preset macos-debug
cmake --build --preset macos-debug --target gpu-triangle-manual

GPU_SAMPLE_EXIT_AFTER_FRAMES="${GPU_SAMPLE_EXIT_AFTER_FRAMES:-1}" \
  "$SAMPLE_BIN" &
pid=$!
elapsed=0

while kill -0 "$pid" 2>/dev/null; do
  if (( elapsed >= TIMEOUT_SECONDS )); then
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
    echo "triangle-manual lifecycle smoke timed out after ${TIMEOUT_SECONDS}s" >&2
    exit 1
  fi
  sleep 1
  elapsed=$((elapsed + 1))
done

wait "$pid"
echo "Lifecycle smoke passed"
