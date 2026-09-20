#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

PROJECT="${1:-examples/reference_game/VesperaReference.vesperaproject}"
CONFIG="${VESPERA_CONFIG:-Development}"
BUILD_DIR="${VESPERA_BUILD_DIR:-build-linux}"
OUTPUT="${VESPERA_OUTPUT:-}"
LAUNCH="${VESPERA_LAUNCH:-0}"

case "$CONFIG" in
  Debug|Development|Release) ;;
  *) echo "VESPERA_CONFIG must be Debug, Development, or Release" >&2; exit 2 ;;
esac

if [[ ! -f "$PROJECT" ]]; then
  echo "Vespera project not found: $PROJECT" >&2
  exit 2
fi
if ! command -v python3 >/dev/null 2>&1; then
  echo "Required tool not found: python3" >&2
  exit 2
fi

if [[ ! -x "$BUILD_DIR/tools/builder/vespera_builder" || ! -x "$BUILD_DIR/runtime/player/vespera_player" ]]; then
  echo "Required Linux build targets are missing; building Vespera first..."
  ./build-linux.sh
fi

if [[ -z "$OUTPUT" ]]; then
  NAME="$(basename "$PROJECT" .vesperaproject | tr ' ' '-')"
  OUTPUT="$(dirname "$PROJECT")/builds/${NAME}-${CONFIG}"
fi

ARGS=(
  --project "$PROJECT"
  --output "$OUTPUT"
  --runtime "$BUILD_DIR/runtime/player/vespera_player"
  --configuration "$CONFIG"
  --sdk-project "$ROOT/managed/Vespera.NET/Vespera.NET.csproj"
  --script-tool-project "$ROOT/managed/Vespera.ScriptTool/Vespera.ScriptTool.csproj"
)
if command -v dotnet >/dev/null 2>&1; then
  ARGS+=(--dotnet "$(command -v dotnet)")
fi
if [[ "$LAUNCH" == "1" ]]; then ARGS+=(--launch); fi

"$BUILD_DIR/tools/builder/vespera_builder" "${ARGS[@]}"
