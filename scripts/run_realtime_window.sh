#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HOST="${HOST:-127.0.0.1}"
PORT="${PORT:-8765}"
cd "${ROOT}"
bash scripts/build_fincept.sh
PYTHONPATH=python python3 -m lobx.realtime_server --host "${HOST}" --port "${PORT}" --build-dir build-fincept
