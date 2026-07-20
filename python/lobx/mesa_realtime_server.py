from __future__ import annotations

import argparse
from collections import Counter, deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import queue
import subprocess
import sys
import threading
import time
from typing import Any
from urllib.parse import parse_qs, urlparse

ROOT = Path(__file__).resolve().parents[2]
HTML_PATH = ROOT / "web" / "mesa_realtime.html"
DEFAULT_INTERVALS = (1, 5, 15, 60)


class StepCandleAggregator:
    def __init__(self, intervals: tuple[int, ...] = DEFAULT_INTERVALS) -> None:
        self.intervals = intervals
        self.candles: dict[tuple[int, int], dict[str, Any]] = {}

    def update(self, step: int, trades: list[dict[str, Any]]) -> list[dict[str, Any]]:
        if not trades:
            return []
        changed: list[dict[str, Any]] = []
        changed_keys: set[tuple[int, int]] = set()
        for interval in self.intervals:
            open_step = ((step - 1) // interval) * interval + 1
            key = (interval, open_step)
            candle = self.candles.get(key)
            for trade in trades:
                price = int(trade["price"])
                qty = int(trade["qty"])
                if candle is None:
                    candle = {
                        "type": "candle",
                        "interval_steps": interval,
                        "open_step": open_step,
                        "close_step": open_step + interval - 1,
                        "open": price,
                        "high": price,
                        "low": price,
                        "close": price,
                        "volume": 0,
                        "quote_volume": 0,
                        "trade_count": 0,
                    }
                    self.candles[key] = candle
                candle["high"] = max(int(candle["high"]), price)
                candle["low"] = min(int(candle["low"]), price)
                candle["close"] = price
                candle["volume"] = int(candle["volume"]) + qty
                candle["quote_volume"] = int(candle["quote_volume"]) + price * qty
                candle["trade_count"] = int(candle["trade_count"]) + 1
            changed_keys.add(key)
        for key in sorted(changed_keys):
            changed.append(dict(self.candles[key]))
        return changed


class MesaRealtimeManager:
    def __init__(self, build_dir: str) -> None:
        self.build_dir = build_dir
        self.clients: list[queue.Queue[str | None]] = []
        self.lock = threading.Lock()
        self.runner: threading.Thread | None = None
        self.stop_event = threading.Event()
        self.running = False
        self.last_params: tuple[Any, ...] | None = None
        self.seq = 0
        self.last_stats: dict[str, Any] = {}
        self.counts: Counter[str] = Counter()

    def is_running(self) -> bool:
        return self.running and self.runner is not None and self.runner.is_alive()

    def start(self, params: dict[str, int | str], restart: bool = False) -> None:
        param_tuple = tuple(sorted(params.items()))
        with self.lock:
            if self.is_running() and not restart and self.last_params == param_tuple:
                return
            if self.is_running():
                self._stop_locked()
            self.stop_event = threading.Event()
            self.running = True
            self.last_params = param_tuple
            self.seq = 0
            self.counts.clear()
            self.last_stats = {}
            self.runner = threading.Thread(target=self._run, args=(params, self.stop_event), daemon=True)
            self.runner.start()

    def stop(self) -> None:
        with self.lock:
            self._stop_locked()

    def _stop_locked(self) -> None:
        self.stop_event.set()
        self.running = False
        self._broadcast(None)

    def subscribe(self) -> queue.Queue[str | None]:
        q: queue.Queue[str | None] = queue.Queue(maxsize=1000)
        with self.lock:
            self.clients.append(q)
        return q

    def unsubscribe(self, q: queue.Queue[str | None]) -> None:
        with self.lock:
            if q in self.clients:
                self.clients.remove(q)

    def snapshot(self) -> dict[str, Any]:
        return {
            "running": self.is_running(),
            "last_stats": self.last_stats,
            "counts": dict(self.counts),
        }

    def _run(self, params: dict[str, int | str], stop_event: threading.Event) -> None:
        if str(params.get("engine", "cpp")) != "cpp":
            self._emit([{"type": "error", "reason": "only engine=cpp is supported"}])
            with self.lock:
                self.running = False
            self._broadcast(None)
            return
        self._run_cpp(params, stop_event)

    def _run_mesa(self, params: dict[str, int | str], stop_event: threading.Event) -> None:
        from .mesa_model import CryptoExchangeModel

        model: CryptoExchangeModel | None = None
        candles = StepCandleAggregator()
        total_steps = int(params["steps"])
        sleep_ms = int(params["sleep_ms"])
        try:
            model = CryptoExchangeModel(
                seed=int(params["seed"]),
                build_dir=self.build_dir,
                makers=int(params["makers"]),
                noise=int(params["noise"]),
                momentum=int(params["momentum"]),
                mean_reversion=int(params["mean_reversion"]),
                whales=int(params["whales"]),
                reference_price=int(params["reference_price"]),
            )
            self._emit(
                [
                    {
                        "type": "agent_mix",
                        "agents": len(model.agents),
                        "agent_types": dict(sorted(model.agent_type_counts.items())),
                    }
                ]
            )

            while not stop_event.is_set() and (total_steps <= 0 or model.now < total_steps):
                before_trades = len(model.trades)
                model.step()
                new_trades = [dict(t) for t in model.trades[before_trades:]]
                events: list[dict[str, Any]] = []
                for trade in new_trades:
                    trade["type"] = "trade"
                    trade["step"] = model.now
                    events.append(trade)
                events.extend(candles.update(model.now, new_trades))
                events.append(self._stats(model))
                self._emit(events)
                if sleep_ms > 0:
                    time.sleep(sleep_ms / 1000.0)
        except Exception as exc:
            self._emit([{"type": "error", "reason": str(exc)}])
        finally:
            if model is not None:
                model.close()
            with self.lock:
                self.running = False
            self._broadcast(None)

    def _run_cpp(self, params: dict[str, int | str], stop_event: threading.Event) -> None:
        exe = ROOT / self.build_dir / "lobx_mesa_agent_simulator"
        if not exe.exists():
            self._emit([{"type": "error", "reason": f"C++ simulator not found: {exe}"}])
            with self.lock:
                self.running = False
            self._broadcast(None)
            return

        total_steps = int(params["steps"])
        if total_steps <= 0:
            total_steps = 2000
        cmd = [
            str(exe),
            "--jsonl",
            "--steps", str(total_steps),
            "--sleep-ms", str(int(params["sleep_ms"])),
            "--seed", str(int(params["seed"])),
            "--reference-price", str(int(params["reference_price"])),
            "--makers", str(int(params["makers"])),
            "--noise", str(int(params["noise"])),
            "--momentum", str(int(params["momentum"])),
            "--mean-reversion", str(int(params["mean_reversion"])),
            "--whales", str(int(params["whales"])),
        ]
        proc: subprocess.Popen[str] | None = None
        try:
            proc = subprocess.Popen(
                cmd,
                cwd=ROOT,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                bufsize=1,
            )
            assert proc.stdout is not None
            while not stop_event.is_set():
                line = proc.stdout.readline()
                if not line:
                    break
                event = json.loads(line)
                self._emit([event])
            if stop_event.is_set() and proc.poll() is None:
                proc.terminate()
            if proc.poll() is None:
                proc.wait(timeout=2)
            if proc.returncode not in (0, None):
                stderr = ""
                if proc.stderr is not None:
                    stderr = proc.stderr.read().strip()
                if stderr:
                    self._emit([{"type": "error", "reason": stderr}])
        except Exception as exc:
            self._emit([{"type": "error", "reason": str(exc)}])
            if proc is not None and proc.poll() is None:
                proc.terminate()
        finally:
            with self.lock:
                self.running = False
            self._broadcast(None)

    def _stats(self, model: CryptoExchangeModel) -> dict[str, Any]:
        spread = model.current_spread()
        stats = {
            "type": "stats",
            "step": model.now,
            "last_price": model.last_price(),
            "best_bid": model.best_bid() or 0,
            "best_ask": model.best_ask() or 0,
            "spread": spread if spread is not None else 0,
            "accepted_orders": model.accepted_orders,
            "rejected_orders": model.rejected_orders,
            "trade_count": len(model.trades),
            "agent_count": len(model.agents),
            "mean_spread": float(model.summary().mean_spread),
        }
        self.last_stats = stats
        return stats

    def _emit(self, events: list[dict[str, Any]]) -> None:
        if not events:
            return
        for event in events:
            self.counts[str(event.get("type", "unknown"))] += 1
            if event.get("type") == "stats":
                self.last_stats = event
        self.seq += 1
        payload = json.dumps({"type": "batch", "seq": self.seq, "events": events}, separators=(",", ":"))
        self._broadcast(payload)

    def _broadcast(self, item: str | None) -> None:
        dead: list[queue.Queue[str | None]] = []
        for q in list(self.clients):
            try:
                q.put_nowait(item)
            except queue.Full:
                dead.append(q)
        for q in dead:
            if q in self.clients:
                self.clients.remove(q)


def parse_int_param(params: dict[str, list[str]], name: str, fallback: int, minimum: int | None = None) -> int:
    raw = params.get(name, [str(fallback)])[0]
    value = int(raw)
    if minimum is not None and value < minimum:
        return minimum
    return value


def parse_run_params(query: str) -> dict[str, int | str]:
    params = parse_qs(query)
    engine = params.get("engine", ["cpp"])[0].lower()
    if engine != "cpp":
        raise ValueError("only engine=cpp is supported for /events")
    return {
        "engine": "cpp",
        "steps": parse_int_param(params, "steps", 0, 0),
        "sleep_ms": parse_int_param(params, "sleep_ms", 120, 0),
        "seed": parse_int_param(params, "seed", 42),
        "reference_price": parse_int_param(params, "reference_price", 100, 1),
        "makers": parse_int_param(params, "makers", 6, 0),
        "noise": parse_int_param(params, "noise", 12, 0),
        "momentum": parse_int_param(params, "momentum", 3, 0),
        "mean_reversion": parse_int_param(params, "mean_reversion", 3, 0),
        "whales": parse_int_param(params, "whales", 1, 0),
    }


class MesaRealtimeHandler(BaseHTTPRequestHandler):
    server_version = "LOBXMesaRealtime/0.1"

    @property
    def manager(self) -> MesaRealtimeManager:
        return self.server.manager  # type: ignore[attr-defined]

    def log_message(self, fmt: str, *args: object) -> None:
        sys.stderr.write("%s - - [%s] %s\n" % (self.client_address[0], self.log_date_time_string(), fmt % args))

    def do_GET(self) -> None:
        parsed = urlparse(self.path)
        if parsed.path in ("/", "/index.html"):
            self.serve_html()
        elif parsed.path == "/events":
            self.serve_events(parsed.query)
        elif parsed.path == "/health":
            self.write_json({"ok": True, "running": self.manager.is_running()})
        elif parsed.path == "/api/snapshot":
            self.write_json(self.manager.snapshot())
        else:
            self.send_error(404)

    def do_POST(self) -> None:
        parsed = urlparse(self.path)
        if parsed.path == "/api/stop":
            self.manager.stop()
            self.write_json({"ok": True})
        else:
            self.send_error(404)

    def serve_html(self) -> None:
        data = HTML_PATH.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def serve_events(self, query: str) -> None:
        params = parse_qs(query)
        try:
            run_params = parse_run_params(query)
        except ValueError as exc:
            self.send_error(400, str(exc))
            return
        restart = params.get("restart", ["0"])[0] == "1"
        q = self.manager.subscribe()
        try:
            self.manager.start(run_params, restart=restart)
        except Exception as exc:
            self.manager.unsubscribe(q)
            self.send_error(500, str(exc))
            return

        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Connection", "keep-alive")
        self.send_header("X-Accel-Buffering", "no")
        self.end_headers()
        try:
            while True:
                try:
                    item = q.get(timeout=15)
                except queue.Empty:
                    self.wfile.write(b": heartbeat\n\n")
                    self.wfile.flush()
                    continue
                if item is None:
                    self.wfile.write(b"data: {\"type\":\"status\",\"running\":false}\n\n")
                    self.wfile.flush()
                    break
                self.wfile.write(f"data: {item}\n\n".encode("utf-8"))
                self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError):
            pass
        finally:
            self.manager.unsubscribe(q)

    def write_json(self, data: dict[str, Any], status: int = 200) -> None:
        body = (json.dumps(data) + "\n").encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Serve the Mesa realtime K-line window")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8770)
    parser.add_argument("--build-dir", default="build-fincept")
    parser.add_argument("--build", action="store_true")
    args = parser.parse_args(argv)

    if args.build:
        subprocess.run(["cmake", "--build", str(ROOT / args.build_dir), "--target", "lobx_step_exchange"], check=True, cwd=ROOT)
        subprocess.run(["cmake", "--build", str(ROOT / args.build_dir), "--target", "lobx_mesa_agent_simulator"], check=True, cwd=ROOT)

    httpd = ThreadingHTTPServer((args.host, args.port), MesaRealtimeHandler)
    httpd.manager = MesaRealtimeManager(args.build_dir)  # type: ignore[attr-defined]
    print(f"LOBX Mesa realtime window: http://{args.host}:{args.port}", flush=True)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        return 0
    finally:
        httpd.manager.stop()  # type: ignore[attr-defined]
        httpd.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
