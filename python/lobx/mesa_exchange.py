from __future__ import annotations

from pathlib import Path
import json
import subprocess
from typing import Any


ROOT = Path(__file__).resolve().parents[2]


class StepExchangeError(RuntimeError):
    pass


class LobxStepExchange:
    """Small process wrapper around the C++ exchange core.

    This wrapper intentionally has no robot behavior. Mesa agents decide actions;
    the C++ side only validates, matches, settles, and reports market state.
    """

    def __init__(self, build_dir: str = "build-fincept") -> None:
        self.exe = ROOT / build_dir / "lobx_step_exchange"
        if not self.exe.exists():
            raise StepExchangeError(f"step exchange not found: {self.exe}")
        self.proc = subprocess.Popen(
            [str(self.exe)],
            cwd=ROOT,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
        )
        ready = self._read()
        if ready.get("type") != "ready":
            raise StepExchangeError(f"unexpected startup response: {ready}")

    def close(self) -> None:
        proc = getattr(self, "proc", None)
        if proc is None:
            return
        if proc.poll() is None:
            try:
                self._send("STOP")
            except Exception:
                pass
            try:
                proc.terminate()
                proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                proc.kill()
        self.proc = None

    def __enter__(self) -> "LobxStepExchange":
        return self

    def __exit__(self, *_: object) -> None:
        self.close()

    def deposit(self, user: int, asset: str, amount: int) -> dict[str, Any]:
        return self._send(f"DEPOSIT,{user},{asset},{amount}")

    def book(self, levels: int = 10) -> dict[str, Any]:
        return self._send(f"BOOK,{levels}")

    def balance(self, user: int, asset: str) -> dict[str, Any]:
        return self._send(f"BALANCE,{user},{asset}")

    def order(
        self,
        user: int,
        order_id: int,
        side: str,
        price: int,
        qty: int,
        flags: str,
        ts: int,
    ) -> dict[str, Any]:
        side = side.upper()
        if side not in {"BUY", "SELL", "BID", "ASK"}:
            raise ValueError(f"invalid side: {side}")
        if price <= 0:
            raise ValueError("price must be positive")
        if qty <= 0:
            raise ValueError("qty must be positive")
        return self._send(f"ORDER,{user},{order_id},{side},{price},{qty},{flags},{ts}")

    def cancel(self, user: int, order_id: int, ts: int) -> dict[str, Any]:
        return self._send(f"CANCEL,{user},{order_id},{ts}")

    def flush(self) -> dict[str, Any]:
        return self._send("FLUSH")

    def _send(self, command: str) -> dict[str, Any]:
        if self.proc is None or self.proc.stdin is None or self.proc.poll() is not None:
            raise StepExchangeError("step exchange is not running")
        self.proc.stdin.write(command + "\n")
        self.proc.stdin.flush()
        response = self._read()
        if response.get("type") == "error":
            raise StepExchangeError(str(response.get("reason", "unknown error")))
        return response

    def _read(self) -> dict[str, Any]:
        if self.proc is None or self.proc.stdout is None:
            raise StepExchangeError("step exchange stdout is not available")
        line = self.proc.stdout.readline()
        if not line:
            stderr = ""
            if self.proc.stderr is not None:
                stderr = self.proc.stderr.read()
            raise StepExchangeError(f"step exchange exited unexpectedly: {stderr.strip()}")
        return json.loads(line)
