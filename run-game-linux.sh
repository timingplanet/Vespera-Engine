#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${VESPERA_BUILD_DIR:-build-linux}"
BINARY="$ROOT/$BUILD_DIR/examples/reference_game/vespera_reference_game"
if [[ ! -x "$BINARY" ]]; then
  echo "Vespera reference game is not built at $BINARY" >&2
  echo "Run ./build-linux.sh first." >&2
  exit 2
fi
cd "$(dirname "$BINARY")"
exec "$BINARY" --renderer=vulkan "$@"
