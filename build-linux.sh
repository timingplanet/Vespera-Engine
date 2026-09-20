#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

for tool in cmake ninja python3; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "Required Linux build tool not found: $tool" >&2
    exit 2
  fi
done

CONFIG="${VESPERA_CONFIG:-Debug}"
BUILD_DIR="${VESPERA_BUILD_DIR:-build-linux}"
STRICT="${VESPERA_STRICT:-0}"
case "$CONFIG" in
  Debug|Development|Release) ;;
  *) echo "VESPERA_CONFIG must be Debug, Development, or Release" >&2; exit 2 ;;
esac
case "$STRICT" in
  0|1) ;;
  *) echo "VESPERA_STRICT must be 0 or 1" >&2; exit 2 ;;
esac

cmake -S . -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE="$CONFIG" \
  -DVESPERA_VULKAN_RENDERER=ON \
  -DVESPERA_BUILD_EDITOR=ON \
  -DVESPERA_BUILD_EXAMPLES=ON \
  -DVESPERA_BUILD_TOOLS=ON \
  -DVESPERA_RMLUI_UI=ON \
  -DVESPERA_LUA_SCRIPTING=ON \
  -DVESPERA_WARNINGS_AS_ERRORS="$STRICT"
cmake --build "$BUILD_DIR"

if command -v dotnet >/dev/null 2>&1; then
  python3 tools/build-managed-runtime.py \
    --stage "$BUILD_DIR/examples/reference_game/managed" \
    --configuration "$CONFIG" \
    --framework net10.0
else
  echo "dotnet not found; native/Lua runtime built, C# stage skipped." >&2
fi

echo "Linux build ready in $BUILD_DIR ($CONFIG, strict=$STRICT)"
