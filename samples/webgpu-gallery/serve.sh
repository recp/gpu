#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DEV_DIR="$SCRIPT_DIR/.dev"
ROOT="${1:-${GPU_WEBGPU_ROOT:-/tmp/gpu-webgpu-typed/samples/webgpu}}"
PORT="${GPU_WEBGPU_PORT:-8765}"
IP="$(ipconfig getifaddr en0 2>/dev/null ||
      ipconfig getifaddr en1 2>/dev/null || true)"

if [ -z "$IP" ]; then
  echo "WebGPU gallery: local network IP not found" >&2
  exit 1
fi
if [ ! -d "$ROOT" ]; then
  echo "WebGPU gallery: build output not found: $ROOT" >&2
  exit 1
fi
for tool in caddy mkcert openssl; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "WebGPU gallery: missing $tool" >&2
    exit 1
  fi
done

mkdir -p "$DEV_DIR"
CERT="$DEV_DIR/dev.crt"
KEY="$DEV_DIR/dev.key"
CA_ROOT="$(mkcert -CAROOT)"
if [ ! -f "$CERT" ] ||
   ! openssl x509 -in "$CERT" -noout -text 2>/dev/null |
     grep -Fq "IP Address:$IP"; then
  if [ ! -f "$CA_ROOT/rootCA.pem" ]; then
    TRUST_STORES=system mkcert -install
  fi
  TRUST_STORES=system mkcert -cert-file "$CERT" -key-file "$KEY" \
    localhost 127.0.0.1 "$IP"
fi

cat > "$DEV_DIR/Caddyfile" <<'EOF'
{
  auto_https disable_redirects
}

https://:{$GPU_WEBGPU_PORT} {
  tls {$GPU_WEBGPU_CERT} {$GPU_WEBGPU_KEY}
  root * {$GPU_WEBGPU_ROOT}
  header Cache-Control "no-store"
  encode zstd gzip
  file_server
}
EOF
caddy fmt --overwrite "$DEV_DIR/Caddyfile" >/dev/null

export GPU_WEBGPU_CERT="$CERT"
export GPU_WEBGPU_KEY="$KEY"
export GPU_WEBGPU_ROOT="$ROOT"
export GPU_WEBGPU_PORT="$PORT"

echo "WebGPU gallery"
echo "  local: https://localhost:$PORT/"
echo "  lan:   https://$IP:$PORT/"
echo "  CA:    $CA_ROOT/rootCA.pem"
exec caddy run --config "$DEV_DIR/Caddyfile" --adapter caddyfile
