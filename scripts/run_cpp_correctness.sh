#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-build}"

cd "$ROOT"

cmake --build "$BUILD_DIR" --target lobx_smoke lobx_perp_risk_tests lobx_perp_fee_tests lobx_perp_funding_tests lobx_perp_simulate_fill_tests lobx_perp_insurance_adl_tests lobx_perp_order_types_tests lobx_perp_trigger_orders_tests
ctest --test-dir "$BUILD_DIR" -R 'lobx_smoke|lobx_perp_risk_tests|lobx_perp_fee_tests|lobx_perp_funding_tests|lobx_perp_simulate_fill_tests|lobx_perp_insurance_adl_tests|lobx_perp_order_types_tests|lobx_perp_trigger_orders_tests' --output-on-failure
