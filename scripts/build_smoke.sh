#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOB_REPO="${LOB_REPO:-${ROOT}/third_party/limit-order-book}"
CXX_BIN="${CXX:-}"

if [[ -z "${CXX_BIN}" ]]; then
  if command -v g++ >/dev/null 2>&1; then
    CXX_BIN="$(command -v g++)"
  elif command -v clang++ >/dev/null 2>&1; then
    CXX_BIN="$(command -v clang++)"
  else
    echo "C++ compiler not found; install GCC/Clang or set CXX" >&2
    exit 1
  fi
fi

mkdir -p "${ROOT}/build"
"${CXX_BIN}" -std=c++20 -O2 -Wall -Wextra -Wpedantic \
  -I"${ROOT}/cpp/include" \
  -I"${LOB_REPO}/cpp/include" \
  "${LOB_REPO}/cpp/src/book_core.cpp" \
  "${LOB_REPO}/cpp/src/price_levels.cpp" \
  "${ROOT}"/cpp/src/*.cpp \
  "${ROOT}/cpp/tests/smoke.cpp" \
  -o "${ROOT}/build/lobx_smoke"

"${ROOT}/build/lobx_smoke"
