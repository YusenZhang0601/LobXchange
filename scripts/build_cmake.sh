#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT}/build}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
JOBS="${JOBS:-4}"

cmake_args=(
  -S "${ROOT}"
  -B "${BUILD_DIR}"
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
)

if command -v ninja >/dev/null 2>&1; then
  cmake_args+=( -G Ninja )
fi

cmake "${cmake_args[@]}"
cmake --build "${BUILD_DIR}" -j "${JOBS}"

if [[ "${RUN_TESTS:-1}" == "1" ]]; then
  ctest --test-dir "${BUILD_DIR}" --output-on-failure
fi
