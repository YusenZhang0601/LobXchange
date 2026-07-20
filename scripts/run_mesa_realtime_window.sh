#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HOST="${HOST:-127.0.0.1}"
PORT="${PORT:-8770}"
PYTHON_BIN="${PYTHON_BIN:-python3}"

cd "${ROOT}"
cmake --build build-fincept --target lobx_step_exchange
cmake --build build-fincept --target lobx_mesa_agent_simulator
PYTHONPATH=python "${PYTHON_BIN}" -m lobx.cli mesa-realtime --host "${HOST}" --port "${PORT}" --build-dir build-fincept
