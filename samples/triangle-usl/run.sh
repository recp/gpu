#!/bin/zsh
set -euo pipefail

SAMPLE_DIR="$(cd "$(dirname "$0")" && pwd)"

"$SAMPLE_DIR/build.sh"
cd "$SAMPLE_DIR"
./hello-triangle-usl
