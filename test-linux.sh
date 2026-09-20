#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

for tool in cmake python3; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "Required tool not found: $tool" >&2
    exit 2
  fi
done

GENERATOR_ARGS=()
if command -v ninja >/dev/null 2>&1; then
  GENERATOR_ARGS=(-G Ninja)
fi

python3 tools/validate_public_release.py

BUILD_DIR="${VESPERA_TEST_BUILD_DIR:-build-tests-linux}"
rm -rf "$BUILD_DIR"
cmake -S . -B "$BUILD_DIR" "${GENERATOR_ARGS[@]}" \
  -DVESPERA_TESTS_ONLY=ON \
  -DVESPERA_WARNINGS_AS_ERRORS=ON
cmake --build "$BUILD_DIR"
ctest --test-dir "$BUILD_DIR" --output-on-failure
python3 tools/validate_source.py
python3 tools/test-release-tooling.py

echo "Vespera Linux/source gate passed."
