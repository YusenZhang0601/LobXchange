from __future__ import annotations

import argparse
from dataclasses import asdict
import json
from pathlib import Path
import subprocess

from .config import ExchangeBootstrap
from .liquidity import fixed_ladder


def cmd_init_config(args: argparse.Namespace) -> int:
    cfg = ExchangeBootstrap.default_demo()
    cfg.save(args.output)
    print(args.output)
    return 0


def cmd_ladder(args: argparse.Namespace) -> int:
    orders = fixed_ladder(
        mid_price=args.mid_price,
        levels=args.levels,
        spread_bps=args.spread_bps,
        qty_per_level=args.qty,
        price_step_bps=args.step_bps,
    )
    print(json.dumps([asdict(order) for order in orders], indent=2))
    return 0


def project_root() -> Path:
    return Path(__file__).resolve().parents[2]


def cmd_simulate_demo(args: argparse.Namespace) -> int:
    root = project_root()
    exe = root / args.build_dir / "lobx_simulator"
    if args.build:
        subprocess.run(["bash", str(root / "scripts" / "build_fincept.sh")], check=True, cwd=root)
    if not exe.exists():
        raise SystemExit(f"simulator not found: {exe}; run bash scripts/build_fincept.sh first")

    cmd = [str(exe)]
    if args.orders:
        cmd += ["--orders", str(args.orders)]
    if args.trades_out:
        cmd += ["--trades-out", str(args.trades_out)]
    if args.candles_out:
        cmd += ["--candles-out", str(args.candles_out)]
    if args.quiet:
        cmd.append("--quiet")
    return subprocess.run(cmd, cwd=root).returncode


def cmd_mesa_smoke(args: argparse.Namespace) -> int:
    from .mesa_model import run_smoke

    summary = run_smoke(args)
    print(json.dumps(asdict(summary), indent=2, sort_keys=True))
    return 0 if summary.trade_count > 0 and summary.accepted_orders > 0 else 1


def cmd_mesa_realtime(args: argparse.Namespace) -> int:
    from .mesa_realtime_server import main as realtime_main

    argv = ["--host", args.host, "--port", str(args.port), "--build-dir", args.build_dir]
    if args.build:
        argv.append("--build")
    return realtime_main(argv)


def cmd_mesa_cpp_smoke(args: argparse.Namespace) -> int:
    root = project_root()
    exe = root / args.build_dir / "lobx_mesa_agent_simulator"
    if args.build:
        subprocess.run(["cmake", "--build", args.build_dir, "--target", "lobx_mesa_agent_simulator"], check=True, cwd=root)
    if not exe.exists():
        raise SystemExit(f"mesa C++ simulator not found: {exe}; build target lobx_mesa_agent_simulator first")

    cmd = [
        str(exe),
        "--steps", str(args.steps),
        "--seed", str(args.seed),
        "--reference-price", str(args.reference_price),
        "--makers", str(args.makers),
        "--noise", str(args.noise),
        "--momentum", str(args.momentum),
        "--mean-reversion", str(args.mean_reversion),
        "--whales", str(args.whales),
    ]
    if args.jsonl:
        cmd.append("--jsonl")
    return subprocess.run(cmd, cwd=root).returncode


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="lobx", description="LOBX exchange bootstrap utilities")
    sub = parser.add_subparsers(dest="command", required=True)

    init = sub.add_parser("init-config", help="write a demo bootstrap JSON config")
    init.add_argument("output", type=Path)
    init.set_defaults(func=cmd_init_config)

    ladder = sub.add_parser("ladder", help="print symmetric post-only LP ladder orders")
    ladder.add_argument("--mid-price", type=int, required=True)
    ladder.add_argument("--levels", type=int, default=5)
    ladder.add_argument("--spread-bps", type=int, default=20)
    ladder.add_argument("--step-bps", type=int, default=10)
    ladder.add_argument("--qty", type=int, default=10)
    ladder.set_defaults(func=cmd_ladder)


    simulate = sub.add_parser("simulate-demo", help="run the compiled C++ issue/order/Kline simulator")
    simulate.add_argument("--build", action="store_true", help="build with scripts/build_fincept.sh before running")
    simulate.add_argument("--build-dir", default="build-fincept")
    simulate.add_argument("--orders", type=Path, help="optional CSV: ts,user,order_id,side,price,qty,flags")
    simulate.add_argument("--trades-out", type=Path)
    simulate.add_argument("--candles-out", type=Path)
    simulate.add_argument("--quiet", action="store_true")
    simulate.set_defaults(func=cmd_simulate_demo)

    mesa_smoke = sub.add_parser("mesa-smoke", help="run Mesa-only robot smoke test against C++ exchange core")
    mesa_smoke.add_argument("--build-dir", default="build-fincept")
    mesa_smoke.add_argument("--steps", type=int, default=80)
    mesa_smoke.add_argument("--seed", type=int, default=42)
    mesa_smoke.add_argument("--reference-price", type=int, default=100)
    mesa_smoke.add_argument("--makers", type=int, default=4)
    mesa_smoke.add_argument("--noise", type=int, default=6)
    mesa_smoke.add_argument("--momentum", type=int, default=2)
    mesa_smoke.add_argument("--mean-reversion", type=int, default=2)
    mesa_smoke.add_argument("--whales", type=int, default=1)
    mesa_smoke.add_argument("--output")
    mesa_smoke.set_defaults(func=cmd_mesa_smoke)

    mesa_realtime = sub.add_parser("mesa-realtime", help="serve Mesa robot realtime K-line browser window")
    mesa_realtime.add_argument("--host", default="127.0.0.1")
    mesa_realtime.add_argument("--port", type=int, default=8770)
    mesa_realtime.add_argument("--build-dir", default="build-fincept")
    mesa_realtime.add_argument("--build", action="store_true")
    mesa_realtime.set_defaults(func=cmd_mesa_realtime)

    mesa_cpp_smoke = sub.add_parser("mesa-cpp-smoke", help="run C++ implementation of the current Mesa robot set")
    mesa_cpp_smoke.add_argument("--build", action="store_true", help="build lobx_mesa_agent_simulator before running")
    mesa_cpp_smoke.add_argument("--build-dir", default="build-fincept")
    mesa_cpp_smoke.add_argument("--steps", type=int, default=80)
    mesa_cpp_smoke.add_argument("--seed", type=int, default=42)
    mesa_cpp_smoke.add_argument("--reference-price", type=int, default=100)
    mesa_cpp_smoke.add_argument("--makers", type=int, default=4)
    mesa_cpp_smoke.add_argument("--noise", type=int, default=6)
    mesa_cpp_smoke.add_argument("--momentum", type=int, default=2)
    mesa_cpp_smoke.add_argument("--mean-reversion", type=int, default=2)
    mesa_cpp_smoke.add_argument("--whales", type=int, default=1)
    mesa_cpp_smoke.add_argument("--jsonl", action="store_true", help="emit JSONL events instead of summary JSON")
    mesa_cpp_smoke.set_defaults(func=cmd_mesa_cpp_smoke)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
