from __future__ import annotations

import argparse
from collections import Counter, deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import queue
import subprocess
import sys
import threading
import time
from urllib.parse import parse_qs, urlparse


ROOT = Path(__file__).resolve().parents[2]
HTML_PATH = ROOT / "web" / "realtime.html"
FINCEPT_ENV = Path(os.environ["FINCEPT_ENV"]) if os.environ.get("FINCEPT_ENV") else None
PLAYER_ID = 9000
UI_BATCH_SECONDS = 0.25
STORE_CHUNK_EVENTS = 5000
STORE_MAX_CHUNKS = 2000
UI_MAX_CANDLES = 240
UI_MAX_TRADES = 40
UI_MAX_ORDERS = 60
UI_MAX_ACCOUNTS = 80
UI_MAX_MISC = 20


def build_env() -> dict[str, str]:
    env = os.environ.copy()
    if FINCEPT_ENV is not None:
        env["PATH"] = f"{FINCEPT_ENV / 'bin'}:{env.get('PATH', '')}"
    return env


class ResultChunkStore:
    def __init__(self, chunk_events: int = STORE_CHUNK_EVENTS, max_chunks: int = STORE_MAX_CHUNKS) -> None:
        self.chunk_events = chunk_events
        self.max_chunks = max_chunks
        self.lock = threading.Lock()
        self.reset()

    def reset(self) -> None:
        with self.lock:
            self.total_events = 0
            self.total_counts: Counter[str] = Counter()
            self.chunks: deque[dict[str, object]] = deque(maxlen=self.max_chunks)
            self.current = self._new_chunk(1)
            self.last_stats: dict[str, object] | None = None
            self.last_price: int | None = None

    def _new_chunk(self, start_seq: int) -> dict[str, object]:
        return {
            "start_seq": start_seq,
            "end_seq": start_seq - 1,
            "events": 0,
            "counts": {},
            "first_step": None,
            "last_step": None,
            "last_price": None,
            "created_at": time.time(),
            "closed_at": None,
        }

    def add(self, obj: dict[str, object]) -> None:
        typ = str(obj.get("type", "unknown"))
        with self.lock:
            self.total_events += 1
            seq = self.total_events
            self.total_counts[typ] += 1
            chunk = self.current
            chunk["end_seq"] = seq
            chunk["events"] = int(chunk["events"]) + 1
            counts = dict(chunk["counts"])
            counts[typ] = int(counts.get(typ, 0)) + 1
            chunk["counts"] = counts
            if typ == "stats":
                self.last_stats = obj
                step = obj.get("step")
                chunk["last_step"] = step
                if chunk["first_step"] is None:
                    chunk["first_step"] = step
                if "last_price" in obj:
                    self.last_price = int(obj["last_price"])
                    chunk["last_price"] = self.last_price
            elif typ == "trade" and "price" in obj:
                self.last_price = int(obj["price"])
                chunk["last_price"] = self.last_price
            if int(chunk["events"]) >= self.chunk_events:
                chunk["closed_at"] = time.time()
                self.chunks.append(chunk)
                self.current = self._new_chunk(seq + 1)

    def summary(self) -> dict[str, object]:
        with self.lock:
            current_events = int(self.current["events"])
            chunks = list(self.chunks)
            if current_events:
                chunks.append(dict(self.current))
            return {
                "total_events": self.total_events,
                "stored_chunks": len(chunks),
                "chunk_events": self.chunk_events,
                "counts": dict(self.total_counts),
                "last_price": self.last_price,
                "last_stats": self.last_stats,
                "recent_chunks": chunks[-12:],
            }


class UiBatch:
    def __init__(self) -> None:
        self.users: deque[dict[str, object]] = deque(maxlen=UI_MAX_ACCOUNTS)
        self.accounts: deque[dict[str, object]] = deque(maxlen=UI_MAX_ACCOUNTS)
        self.candles: dict[tuple[int, int], dict[str, object]] = {}
        self.trades: deque[dict[str, object]] = deque(maxlen=UI_MAX_TRADES)
        self.orders: deque[dict[str, object]] = deque(maxlen=UI_MAX_ORDERS)
        self.misc: deque[dict[str, object]] = deque(maxlen=UI_MAX_MISC)
        self.stats: dict[str, object] | None = None
        self.dropped: Counter[str] = Counter()
        self.counts: Counter[str] = Counter()

    def empty(self) -> bool:
        return not (self.users or self.accounts or self.candles or self.trades or self.orders or self.misc or self.stats)

    def add(self, obj: dict[str, object]) -> None:
        typ = str(obj.get("type", "unknown"))
        self.counts[typ] += 1
        if typ == "user":
            if len(self.users) >= UI_MAX_ACCOUNTS:
                self.dropped[typ] += 1
            self.users.append(obj)
        elif typ == "account":
            if len(self.accounts) >= UI_MAX_ACCOUNTS:
                self.dropped[typ] += 1
            self.accounts.append(obj)
        elif typ == "stats":
            self.stats = obj
        elif typ == "candle":
            try:
                key = (int(obj["interval_ns"]), int(obj["open_time_ns"]))
            except Exception:
                key = (0, len(self.candles))
            if key not in self.candles and len(self.candles) >= UI_MAX_CANDLES:
                first = next(iter(self.candles))
                self.candles.pop(first, None)
                self.dropped[typ] += 1
            self.candles[key] = obj
        elif typ == "trade":
            if len(self.trades) >= UI_MAX_TRADES:
                self.dropped[typ] += 1
            self.trades.append(obj)
        elif typ == "order":
            if len(self.orders) >= UI_MAX_ORDERS:
                self.dropped[typ] += 1
            self.orders.append(obj)
        else:
            if len(self.misc) >= UI_MAX_MISC:
                self.dropped[typ] += 1
            self.misc.append(obj)

    def payload(self, seq: int, store_summary: dict[str, object]) -> dict[str, object]:
        events: list[dict[str, object]] = []
        events.extend(self.users)
        events.extend(self.accounts)
        events.extend(self.candles.values())
        events.extend(self.trades)
        events.extend(self.orders)
        events.extend(self.misc)
        if self.stats is not None:
            events.append(self.stats)
        return {
            "type": "batch",
            "seq": seq,
            "events": events,
            "event_counts": dict(self.counts),
            "dropped": dict(self.dropped),
            "store": store_summary,
        }


class SimulatorManager:
    def __init__(self, build_dir: str) -> None:
        self.build_dir = build_dir
        self.proc: subprocess.Popen[str] | None = None
        self.clients: list[queue.Queue[str | None]] = []
        self.lock = threading.Lock()
        self.reader: threading.Thread | None = None
        self.last_params: tuple[str, str, str, str] | None = None
        self.store = ResultChunkStore()
        self.batch_seq = 0

    def exe(self) -> Path:
        return ROOT / self.build_dir / "lobx_realtime_simulator"

    def is_running(self) -> bool:
        return self.proc is not None and self.proc.poll() is None

    def start(self, steps: str = "0", sleep_ms: str = "50", seed: str = "42", speed_x: str = "0", restart: bool = False) -> None:
        params = (steps, sleep_ms, seed, speed_x)
        with self.lock:
            if self.is_running() and not restart and self.last_params == params:
                return
            if self.is_running() and (restart or self.last_params != params):
                self._stop_locked()
            exe = self.exe()
            if not exe.exists():
                raise RuntimeError(f"simulator not found: {exe}")
            self.store.reset()
            self.batch_seq = 0
            cmd = [str(exe), "--steps", steps, "--sleep-ms", sleep_ms, "--speed-x", speed_x, "--seed", seed]
            self.proc = subprocess.Popen(
                cmd,
                cwd=ROOT,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                bufsize=1,
                env=build_env(),
            )
            self.last_params = params
            self.reader = threading.Thread(target=self._reader_main, daemon=True)
            self.reader.start()

    def stop(self) -> None:
        with self.lock:
            self._stop_locked()

    def _stop_locked(self) -> None:
        proc = self.proc
        self.proc = None
        if proc is not None and proc.poll() is None:
            try:
                if proc.stdin:
                    proc.stdin.write("STOP\n")
                    proc.stdin.flush()
            except Exception:
                pass
            try:
                proc.terminate()
                proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                proc.kill()
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

    def send_order(self, user: int, side: str, price: int, qty: int, order_type: str = "LIMIT", volatility_bps: int = 100) -> None:
        side = side.upper()
        order_type = order_type.upper()
        if side not in {"BUY", "SELL"}:
            raise ValueError("side must be BUY or SELL")
        if order_type not in {"LIMIT", "MARKET"}:
            raise ValueError("order_type must be LIMIT or MARKET")
        if qty <= 0:
            raise ValueError("qty must be positive")
        if order_type == "LIMIT" and price <= 0:
            raise ValueError("limit price must be positive")
        if volatility_bps < 0:
            raise ValueError("volatility_bps must be non-negative")
        with self.lock:
            if not self.is_running():
                raise RuntimeError("simulator is not running")
            assert self.proc is not None and self.proc.stdin is not None
            if order_type == "MARKET":
                self.proc.stdin.write(f"MARKET,{user},{side},{qty},{volatility_bps}\n")
            else:
                self.proc.stdin.write(f"ORDER,{user},{side},{price},{qty}\n")
            self.proc.stdin.flush()

    def _reader_main(self) -> None:
        proc = self.proc
        if proc is None or proc.stdout is None:
            return
        batch = UiBatch()
        last_emit = time.monotonic()
        try:
            for line in proc.stdout:
                line = line.strip()
                if not line:
                    continue
                try:
                    obj = json.loads(line)
                except json.JSONDecodeError:
                    obj = {"type": "raw", "line": line[:1000]}
                self.store.add(obj)
                batch.add(obj)
                now = time.monotonic()
                if now - last_emit >= UI_BATCH_SECONDS:
                    self._emit_batch(batch)
                    batch = UiBatch()
                    last_emit = now
            self._emit_batch(batch)
        finally:
            self._broadcast(None)

    def _emit_batch(self, batch: UiBatch) -> None:
        if batch.empty():
            return
        self.batch_seq += 1
        payload = batch.payload(self.batch_seq, self.store.summary())
        self._broadcast(json.dumps(payload, separators=(",", ":")))

    def snapshot(self) -> dict[str, object]:
        return self.store.summary()

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


class RealtimeHandler(BaseHTTPRequestHandler):
    server_version = "LOBXRealtime/0.2"

    def log_message(self, fmt: str, *args: object) -> None:
        sys.stderr.write("%s - - [%s] %s\n" % (self.client_address[0], self.log_date_time_string(), fmt % args))

    @property
    def manager(self) -> SimulatorManager:
        return self.server.manager  # type: ignore[attr-defined]

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
        if parsed.path == "/api/order":
            self.api_order()
        elif parsed.path == "/api/stop":
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
        steps = params.get("steps", ["0"])[0]
        sleep_ms = params.get("sleep_ms", ["50"])[0]
        seed = params.get("seed", ["42"])[0]
        speed_x = params.get("speed_x", ["0"])[0]
        restart = params.get("restart", ["0"])[0] == "1"
        if restart:
            self.manager.stop()
        q = self.manager.subscribe()
        try:
            self.manager.start(steps=steps, sleep_ms=sleep_ms, seed=seed, speed_x=speed_x, restart=False)
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

    def api_order(self) -> None:
        try:
            length = int(self.headers.get("Content-Length", "0"))
            payload = json.loads(self.rfile.read(length).decode("utf-8"))
            user = int(payload.get("user", 9000))
            side = str(payload["side"])
            order_type = str(payload.get("order_type", "LIMIT"))
            price = int(payload.get("price", 0))
            qty = int(payload["qty"])
            volatility_bps = int(payload.get("volatility_bps", 100))
            self.manager.send_order(user=user, side=side, price=price, qty=qty, order_type=order_type, volatility_bps=volatility_bps)
            self.write_json({"ok": True})
        except Exception as exc:
            self.write_json({"ok": False, "error": str(exc)}, status=400)

    def write_json(self, data: dict[str, object], status: int = 200) -> None:
        body = (json.dumps(data) + "\n").encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Serve the LOBX realtime trading visualization")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--build-dir", default="build-fincept")
    parser.add_argument("--build", action="store_true")
    args = parser.parse_args(argv)

    if args.build:
        subprocess.run(["bash", str(ROOT / "scripts" / "build_fincept.sh")], check=True, cwd=ROOT, env=build_env())

    httpd = ThreadingHTTPServer((args.host, args.port), RealtimeHandler)
    httpd.manager = SimulatorManager(args.build_dir)  # type: ignore[attr-defined]
    print(f"LOBX realtime trading window: http://{args.host}:{args.port}", flush=True)
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
