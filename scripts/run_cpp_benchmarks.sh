#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-build}"
STAMP="$(date +%Y%m%d_%H%M%S)"
OUT_DIR="${OUT_DIR:-$ROOT/artifacts/cpp_benchmarks/$STAMP}"

PERP_ORDERS="${PERP_ORDERS:-1000}"

cd "$ROOT"
mkdir -p "$OUT_DIR"

cmake --build "$BUILD_DIR" --target lobx_bench_perp

"$ROOT/$BUILD_DIR/lobx_bench_perp" --orders "$PERP_ORDERS" > "$OUT_DIR/bench_perp.json"

printf 'C++ benchmark artifacts: %s\n' "$OUT_DIR"
