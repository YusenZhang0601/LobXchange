#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FINCEPT_ENV="${FINCEPT_ENV:-}"
LOB_REPO="${LOB_REPO:-${ROOT}/third_party/limit-order-book}"
BUILD_DIR="${BUILD_DIR:-${ROOT}/build-fincept}"

cmake_args=(
  -S "${ROOT}"
  -B "${BUILD_DIR}"
  -DLOB_REPO="${LOB_REPO}"
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE:-Release}"
)

if [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]] && command -v ninja >/dev/null 2>&1; then
  cmake_args+=( -G Ninja )
fi

if [[ -n "${FINCEPT_ENV}" ]]; then
  export PATH="${FINCEPT_ENV}/bin:${PATH}"
  if [[ -x "${FINCEPT_ENV}/bin/g++" ]]; then
    cmake_args+=( -DCMAKE_CXX_COMPILER="${FINCEPT_ENV}/bin/g++" )
  fi
  if [[ -x "${FINCEPT_ENV}/bin/x86_64-conda-linux-gnu-ar" ]]; then
    cmake_args+=( -DCMAKE_AR="${FINCEPT_ENV}/bin/x86_64-conda-linux-gnu-ar" )
  fi
  if [[ -x "${FINCEPT_ENV}/bin/x86_64-conda-linux-gnu-ranlib" ]]; then
    cmake_args+=( -DCMAKE_RANLIB="${FINCEPT_ENV}/bin/x86_64-conda-linux-gnu-ranlib" )
  fi
fi

cmake "${cmake_args[@]}"

cmake --build "${BUILD_DIR}" -j "${JOBS:-4}"
if [[ "${RUN_KNOWN_FAILURES:-0}" == "1" ]]; then
  ctest --test-dir "${BUILD_DIR}" --output-on-failure
else
  ctest --test-dir "${BUILD_DIR}" --output-on-failure -LE known_failure
fi
