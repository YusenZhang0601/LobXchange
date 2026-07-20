#!/usr/bin/env python3
"""Plot LOBX long diagnostic experiment bundles."""

from __future__ import annotations

import argparse
import json
import os
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, List, Optional

_CACHE_ROOT = Path(tempfile.gettempdir()) / "lobx_plot_diagnostic_cache"
(_CACHE_ROOT / "matplotlib").mkdir(parents=True, exist_ok=True)
(_CACHE_ROOT / "xdg").mkdir(parents=True, exist_ok=True)
os.environ.setdefault("MPLCONFIGDIR", str(_CACHE_ROOT / "matplotlib"))
os.environ.setdefault("XDG_CACHE_HOME", str(_CACHE_ROOT / "xdg"))

try:
    import pandas as pd
except ImportError as exc:
    raise SystemExit("Please install pandas matplotlib to plot diagnostic bundles.") from exc

try:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
except ImportError as exc:
    raise SystemExit("Please install pandas matplotlib to plot diagnostic bundles.") from exc


@dataclass
class Run:
    path: Path
    metadata: dict
    summary: dict
    accounting: dict

    @property
    def scenario(self) -> str:
        return str(self.metadata.get("scenario") or self.summary.get("scenario") or self.path.parent.name)

    @property
    def seed(self) -> str:
        return str(self.metadata.get("seed") or self.summary.get("seed") or self.path.name.replace("seed=", ""))

    @property
    def label(self) -> str:
        return f"{self.scenario}/seed={self.seed}"


def warn(message: str) -> None:
    print(f"warning: {message}", file=sys.stderr)


def read_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as fh:
        return json.load(fh)


def discover_runs(input_path: Path, scenario: Optional[str], seed: Optional[str], max_runs: Optional[int]) -> List[Run]:
    if not input_path.exists():
        raise SystemExit(f"Input path does not exist: {input_path}")
    if (input_path / "run_metadata.json").exists() or (input_path / "summary.json").exists():
        dirs = [input_path]
    else:
        dirs = sorted({p.parent for p in input_path.rglob("run_metadata.json")})
        dirs += sorted({p.parent for p in input_path.rglob("summary.json")} - set(dirs))
    runs: List[Run] = []
    for run_dir in dirs:
        metadata_path = run_dir / "run_metadata.json"
        summary_path = run_dir / "summary.json"
        accounting_path = run_dir / "accounting_summary.json"
        if not summary_path.exists():
            warn(f"missing summary.json: {run_dir}")
            continue
        metadata = read_json(metadata_path) if metadata_path.exists() else {}
        summary = read_json(summary_path)
        accounting = read_json(accounting_path) if accounting_path.exists() else {}
        run = Run(run_dir, metadata, summary, accounting)
        if scenario and run.scenario != scenario:
            continue
        if seed and run.seed != seed:
            continue
        runs.append(run)
        if max_runs is not None and len(runs) >= max_runs:
            break
    if not runs:
        raise SystemExit("No diagnostic runs matched input/filter options.")
    return runs


STRICT_REQUIRE_ALL = False


def read_csv(run: Run, name: str, required: bool = False) -> Optional[pd.DataFrame]:
    path = run.path / name
    if not path.exists():
        message = f"missing {name}: {run.path}"
        if required or STRICT_REQUIRE_ALL:
            raise SystemExit(message)
        warn(message)
        return None
    df = pd.read_csv(path)
    if df.empty:
        warn(f"empty {name}: {path}")
    return df


def downsample(df: pd.DataFrame, n: int) -> pd.DataFrame:
    if n <= 1 or df.empty:
        return df
    return df.iloc[::n].copy()


def save(output: Path, name: str) -> Path:
    output.mkdir(parents=True, exist_ok=True)
    path = output / name
    plt.tight_layout()
    plt.savefig(path, dpi=150)
    plt.close()
    print(path)
    return path


def plot_price_paths(runs: Iterable[Run], output: Path, ds: int) -> None:
    plt.figure(figsize=(11, 6))
    for run in runs:
        df = downsample(read_csv(run, "price_series.csv", True), ds)
        plt.plot(df["step"], df["mid_price"], label=run.label, linewidth=1.2)
    plt.xlabel("step")
    plt.ylabel("mid_price")
    plt.title("Price paths")
    plt.legend(fontsize=8)
    plt.grid(True, alpha=0.25)
    save(output, "price_paths.png")


def plot_normalized_price_paths(runs: Iterable[Run], output: Path, ds: int) -> None:
    plt.figure(figsize=(11, 6))
    for run in runs:
        df = downsample(read_csv(run, "price_series.csv", True), ds)
        initial = df["mid_price"].iloc[0]
        plt.plot(df["step"], (df["mid_price"] / initial - 1.0) * 10000.0, label=run.label, linewidth=1.2)
    plt.xlabel("step")
    plt.ylabel("return from initial (bps)")
    plt.title("Normalized price paths")
    plt.legend(fontsize=8)
    plt.grid(True, alpha=0.25)
    save(output, "normalized_price_paths.png")


def plot_returns(runs: Iterable[Run], output: Path, ds: int) -> None:
    plt.figure(figsize=(11, 6))
    for run in runs:
        df = downsample(read_csv(run, "price_series.csv", True), ds)
        y = df["step_return_bps"] if "step_return_bps" in df else df["mid_price"].pct_change().fillna(0) * 10000.0
        plt.plot(df["step"], y, label=run.label, linewidth=1.0)
    plt.xlabel("step")
    plt.ylabel("step_return_bps")
    plt.title("Returns timeseries")
    plt.legend(fontsize=8)
    plt.grid(True, alpha=0.25)
    save(output, "returns_timeseries.png")

    plt.figure(figsize=(11, 6))
    for run in runs:
        df = read_csv(run, "price_series.csv", True)
        y = df["step_return_bps"] if "step_return_bps" in df else df["mid_price"].pct_change().fillna(0) * 10000.0
        plt.hist(y.iloc[1:], bins=50, alpha=0.35, label=run.label)
    plt.xlabel("step_return_bps")
    plt.ylabel("count")
    plt.title("Returns histogram")
    plt.legend(fontsize=8)
    plt.grid(True, alpha=0.25)
    save(output, "returns_histogram.png")


def plot_drawdown_spread_volume(runs: Iterable[Run], output: Path, ds: int) -> None:
    plt.figure(figsize=(11, 6))
    for run in runs:
        df = downsample(read_csv(run, "price_series.csv", True), ds)
        y = df["drawdown_bps"] if "drawdown_bps" in df else (df["mid_price"] / df["mid_price"].cummax() - 1.0) * 10000.0
        plt.plot(df["step"], y, label=run.label, linewidth=1.2)
    plt.xlabel("step")
    plt.ylabel("drawdown_bps")
    plt.title("Drawdown paths")
    plt.legend(fontsize=8)
    plt.grid(True, alpha=0.25)
    save(output, "drawdown_paths.png")

    plt.figure(figsize=(11, 6))
    for run in runs:
        df = downsample(read_csv(run, "price_series.csv", True), ds)
        plt.plot(df["step"], df["spread_bps"], label=run.label, linewidth=1.2)
    plt.xlabel("step")
    plt.ylabel("spread_bps")
    plt.title("Spread paths")
    plt.legend(fontsize=8)
    plt.grid(True, alpha=0.25)
    save(output, "spread_paths.png")

    fig, axes = plt.subplots(2, 1, figsize=(11, 8), sharex=True)
    for run in runs:
        df = downsample(read_csv(run, "price_series.csv", True), ds)
        axes[0].plot(df["step"], df.get("step_volume", df["cum_volume"].diff().fillna(0)), label=run.label)
        axes[1].plot(df["step"], df.get("step_trade_count", df["trade_count"].diff().fillna(0)), label=run.label)
    axes[0].set_ylabel("step_volume")
    axes[1].set_ylabel("step_trade_count")
    axes[1].set_xlabel("step")
    axes[0].set_title("Volume and trades")
    axes[0].legend(fontsize=8)
    axes[0].grid(True, alpha=0.25)
    axes[1].grid(True, alpha=0.25)
    save(output, "volume_and_trades.png")


def plot_actions(runs: Iterable[Run], output: Path, ds: int) -> None:
    plt.figure(figsize=(11, 6))
    for run in runs:
        df = read_csv(run, "actions_by_step.csv")
        if df is None:
            continue
        df = downsample(df, ds)
        for col in ["total_actions", "submit_limit_order", "submit_market_order", "cancel_order", "replace_order"]:
            if col in df:
                plt.plot(df["step"], df[col], label=f"{run.label}:{col}", linewidth=0.9)
    plt.xlabel("step")
    plt.ylabel("count")
    plt.title("Action counts by step")
    plt.legend(fontsize=7)
    plt.grid(True, alpha=0.25)
    save(output, "action_counts_by_step.png")

    rows = []
    for run in runs:
        df = read_csv(run, "actions_by_type.csv")
        if df is None or df.empty:
            continue
        grouped = df.groupby("agent_type")["total_actions"].sum().reset_index()
        grouped["run"] = run.label
        rows.append(grouped)
    if rows:
        data = pd.concat(rows)
        pivot = data.pivot_table(index="agent_type", columns="run", values="total_actions", aggfunc="sum").fillna(0)
        pivot.plot(kind="bar", figsize=(11, 6))
        plt.ylabel("total_actions")
        plt.title("Action counts by agent type")
        plt.grid(True, axis="y", alpha=0.25)
        save(output, "action_counts_by_agent_type.png")


def plot_agents(runs: Iterable[Run], output: Path, ds: int) -> None:
    plt.figure(figsize=(11, 6))
    for run in runs:
        df = read_csv(run, "agent_final_state.csv")
        if df is not None and not df.empty:
            plt.hist(df["total_pnl"], bins=40, alpha=0.35, label=run.label)
    plt.xlabel("total_pnl")
    plt.ylabel("agent count")
    plt.title("Agent PnL distribution")
    plt.legend(fontsize=8)
    plt.grid(True, alpha=0.25)
    save(output, "agent_pnl_distribution.png")

    rows = []
    for run in runs:
        df = read_csv(run, "agent_type_summary.csv")
        if df is not None and not df.empty:
            df = df.copy()
            df["run"] = run.label
            rows.append(df)
    if rows:
        data = pd.concat(rows)
        pivot = data.pivot_table(index="agent_type", columns="run", values="mean_pnl", aggfunc="mean").fillna(0)
        pivot.plot(kind="bar", figsize=(11, 6))
        plt.ylabel("mean_pnl")
        plt.title("Agent PnL by type")
        plt.grid(True, axis="y", alpha=0.25)
        save(output, "agent_pnl_by_type.png")

    for metric, filename, title in [
        ("equity", "agent_equity_paths_by_type.png", "Mean equity by agent type"),
        ("inventory_total", "inventory_paths_by_type.png", "Mean inventory by agent type"),
    ]:
        plt.figure(figsize=(11, 6))
        for run in runs:
            df = read_csv(run, "agent_state_samples.csv")
            if df is None or df.empty:
                continue
            df = downsample(df, ds)
            grouped = df.groupby(["step", "agent_type"])[metric].mean().reset_index()
            for agent_type, sub in grouped.groupby("agent_type"):
                plt.plot(sub["step"], sub[metric], label=f"{run.label}:{agent_type}", linewidth=1.0)
        plt.xlabel("step")
        plt.ylabel(metric)
        plt.title(title)
        plt.legend(fontsize=7)
        plt.grid(True, alpha=0.25)
        save(output, filename)

    plt.figure(figsize=(11, 6))
    for run in runs:
        df = read_csv(run, "agent_state_samples.csv")
        if df is None or df.empty:
            continue
        grouped = downsample(df.groupby("step")[["cash_locked", "inventory_locked"]].sum().reset_index(), ds)
        plt.plot(grouped["step"], grouped["cash_locked"], label=f"{run.label}:cash_locked")
        plt.plot(grouped["step"], grouped["inventory_locked"], label=f"{run.label}:inventory_locked")
    plt.xlabel("step")
    plt.ylabel("locked balance")
    plt.title("Locked balance paths")
    plt.legend(fontsize=8)
    plt.grid(True, alpha=0.25)
    save(output, "locked_balance_paths.png")


def plot_accounting_book_events(runs: Iterable[Run], output: Path, ds: int) -> None:
    labels = [run.label for run in runs]
    residuals = [float(run.accounting.get("system_pnl_residual", 0.0) or 0.0) for run in runs]
    plt.figure(figsize=(11, 5))
    plt.bar(labels, residuals)
    plt.ylabel("system_pnl_residual")
    plt.title("Accounting residual")
    plt.xticks(rotation=25, ha="right")
    plt.grid(True, axis="y", alpha=0.25)
    save(output, "accounting_residual.png")

    plt.figure(figsize=(11, 6))
    for run in runs:
        df = read_csv(run, "book_samples.csv")
        if df is None or df.empty:
            continue
        grouped = df.groupby(["step", "side"])["quantity"].sum().reset_index()
        for side, sub in grouped.groupby("side"):
            sub = downsample(sub, ds)
            plt.plot(sub["step"], sub["quantity"], label=f"{run.label}:{side}", linewidth=1.1)
    plt.xlabel("step")
    plt.ylabel("top-N depth")
    plt.title("Book depth paths")
    plt.legend(fontsize=8)
    plt.grid(True, alpha=0.25)
    save(output, "book_depth_paths.png")

    plt.figure(figsize=(11, 6))
    for run in runs:
        df = read_csv(run, "runtime_metrics.csv")
        if df is None or df.empty:
            continue
        df = downsample(df, ds)
        for col in ["total_actions", "total_trades", "total_rejections", "open_order_count"]:
            if col in df:
                plt.plot(df["step"], df[col], label=f"{run.label}:{col}", linewidth=0.9)
    plt.xlabel("step")
    plt.ylabel("count")
    plt.title("Event rate and open orders")
    plt.legend(fontsize=7)
    plt.grid(True, alpha=0.25)
    save(output, "event_rate.png")

    any_impact = False
    plt.figure(figsize=(9, 5))
    for run in runs:
        df = read_csv(run, "impact_windows.csv")
        if df is not None and not df.empty:
            any_impact = True
            plt.hist(df["impact_bps"], bins=30, alpha=0.4, label=run.label)
    if any_impact:
        plt.xlabel("impact_bps")
        plt.title("Impact windows")
        plt.legend(fontsize=8)
        plt.grid(True, alpha=0.25)
        save(output, "impact_windows.png")
    else:
        plt.close()
        warn("impact_windows.csv empty for all runs; impact_windows.png not generated")


def plot_extended_diagnostics(runs: Iterable[Run], output: Path, ds: int) -> None:
    plt.figure(figsize=(11, 6))
    any_data = False
    for run in runs:
        df = read_csv(run, "open_order_growth.csv")
        if df is None or df.empty:
            continue
        any_data = True
        df = downsample(df, ds)
        plt.plot(df["step"], df["total_open_orders"], label=f"{run.label}:total", linewidth=1.1)
        if "max_open_orders_per_agent" in df:
            plt.plot(df["step"], df["max_open_orders_per_agent"], label=f"{run.label}:max/agent", linewidth=0.9)
    if any_data:
        plt.xlabel("step")
        plt.ylabel("open orders")
        plt.title("Open order growth")
        plt.legend(fontsize=8)
        plt.grid(True, alpha=0.25)
        save(output, "open_order_growth.png")
    else:
        plt.close()

    plt.figure(figsize=(11, 6))
    any_data = False
    for run in runs:
        df = read_csv(run, "agent_type_pnl_timeseries.csv")
        if df is None or df.empty:
            continue
        any_data = True
        df = downsample(df, ds)
        for agent_type, sub in df.groupby("agent_type"):
            plt.plot(sub["step"], sub["mean_pnl"], label=f"{run.label}:{agent_type}", linewidth=1.0)
    if any_data:
        plt.xlabel("step")
        plt.ylabel("mean_pnl")
        plt.title("Mean PnL by type timeseries")
        plt.legend(fontsize=7)
        plt.grid(True, alpha=0.25)
        save(output, "mean_pnl_by_type_timeseries.png")
    else:
        plt.close()

    plt.figure(figsize=(11, 6))
    any_data = False
    for run in runs:
        df = read_csv(run, "inventory_consistency_by_agent.csv")
        if df is None or df.empty:
            continue
        any_data = True
        for agent_type, sub in df.groupby("agent_type"):
            plt.scatter(sub["agent_id"], sub["inventory_residual"], label=f"{run.label}:{agent_type}", s=20)
    if any_data:
        plt.xlabel("agent_id")
        plt.ylabel("inventory_residual")
        plt.title("Inventory consistency residual")
        plt.legend(fontsize=7)
        plt.grid(True, alpha=0.25)
        save(output, "inventory_consistency_residual.png")
    else:
        plt.close()

    plt.figure(figsize=(11, 6))
    any_data = False
    phase_cols = ["agent_decide_ms", "action_schedule_ms", "exchange_apply_ms", "state_update_ms", "recorder_ms", "book_sample_ms"]
    for run in runs:
        df = read_csv(run, "runtime_metrics.csv")
        if df is None or df.empty:
            continue
        any_data = True
        df = downsample(df, ds)
        for col in phase_cols:
            if col in df:
                plt.plot(df["step"], df[col], label=f"{run.label}:{col}", linewidth=0.9)
    if any_data:
        plt.xlabel("step")
        plt.ylabel("interval ms")
        plt.title("Runtime phase breakdown")
        plt.legend(fontsize=7)
        plt.grid(True, alpha=0.25)
        save(output, "runtime_phase_breakdown.png")
    else:
        plt.close()

    plt.figure(figsize=(10, 6))
    any_data = False
    for run in runs:
        df = read_csv(run, "agent_final_state.csv")
        if df is None or df.empty:
            continue
        any_data = True
        for agent_type, sub in df.groupby("agent_type"):
            plt.scatter(sub["final_inventory_total"], sub["total_pnl"], label=f"{run.label}:{agent_type}", s=22)
    if any_data:
        plt.xlabel("final_inventory_total")
        plt.ylabel("total_pnl")
        plt.title("PnL vs inventory")
        plt.legend(fontsize=7)
        plt.grid(True, alpha=0.25)
        save(output, "pnl_vs_inventory_scatter.png")
    else:
        plt.close()

    fig, axes = plt.subplots(2, 1, figsize=(11, 8), sharex=True)
    any_data = False
    for run in runs:
        df = read_csv(run, "agent_state_samples.csv")
        if df is None or df.empty:
            continue
        any_data = True
        grouped = df.groupby(["step", "agent_type"])[["cash_locked", "inventory_locked"]].mean().reset_index()
        grouped = downsample(grouped, ds)
        for agent_type, sub in grouped.groupby("agent_type"):
            axes[0].plot(sub["step"], sub["cash_locked"], label=f"{run.label}:{agent_type}", linewidth=0.9)
            axes[1].plot(sub["step"], sub["inventory_locked"], label=f"{run.label}:{agent_type}", linewidth=0.9)
    if any_data:
        axes[0].set_ylabel("mean cash_locked")
        axes[1].set_ylabel("mean inventory_locked")
        axes[1].set_xlabel("step")
        axes[0].set_title("Locked balance by type")
        axes[0].legend(fontsize=7)
        axes[0].grid(True, alpha=0.25)
        axes[1].grid(True, alpha=0.25)
        save(output, "locked_balance_by_type.png")
    else:
        plt.close(fig)

    rows = []
    for run in runs:
        df = read_csv(run, "actions_by_type.csv")
        if df is None or df.empty:
            continue
        grouped = df.groupby("agent_type")["total_actions"].sum().reset_index()
        grouped["run"] = run.label
        rows.append(grouped)
    if rows:
        data = pd.concat(rows)
        pivot = data.pivot_table(index="agent_type", columns="run", values="total_actions", aggfunc="sum").fillna(0)
        pivot.plot(kind="bar", figsize=(11, 6))
        plt.ylabel("total_actions")
        plt.title("Action to trade funnel: actions by type")
        plt.grid(True, axis="y", alpha=0.25)
        save(output, "action_to_trade_funnel.png")

    rows = []
    for run in runs:
        path = run.path / "perf_summary.json"
        if not path.exists():
            if STRICT_REQUIRE_ALL:
                raise SystemExit(f"missing perf_summary.json: {run.path}")
            warn(f"missing perf_summary.json: {run.path}")
            continue
        perf = read_json(path)
        for metric in ["total_elapsed_ms", "agent_decide_ms", "exchange_apply_ms", "recorder_ms", "max_open_orders", "actions_per_sec", "orders_per_sec"]:
            rows.append({"run": run.label, "metric": metric, "value": perf.get(metric, 0)})
    if rows:
        data = pd.DataFrame(rows)
        pivot = data.pivot_table(index="metric", columns="run", values="value", aggfunc="mean").fillna(0)
        pivot.plot(kind="bar", figsize=(12, 6))
        plt.ylabel("value")
        plt.title("Performance summary")
        plt.grid(True, axis="y", alpha=0.25)
        save(output, "perf_summary_bar.png")


def write_plot_summary(runs: Iterable[Run], output: Path) -> None:
    rows = []
    for run in runs:
        perf = read_json(run.path / "perf_summary.json") if (run.path / "perf_summary.json").exists() else {}
        inv = read_json(run.path / "inventory_consistency_summary.json") if (run.path / "inventory_consistency_summary.json").exists() else {}
        rows.append(
            {
                "run_label": run.label,
                "scenario": run.scenario,
                "seed": run.seed,
                "agent_count": run.summary.get("agent_count", run.metadata.get("agent_count", "")),
                "steps": run.summary.get("steps", run.metadata.get("steps", "")),
                "total_return_bps": run.summary.get("total_return_bps", 0),
                "realized_vol_bps": run.summary.get("realized_vol_bps", 0),
                "max_drawdown_bps": run.summary.get("max_drawdown_bps", 0),
                "largest_abs_return_bps": run.summary.get("largest_abs_return_bps", 0),
                "sum_agent_pnl": run.accounting.get("sum_agent_pnl", 0),
                "system_pnl_residual": run.accounting.get("system_pnl_residual", 0),
                "negative_pnl_agent_count": run.accounting.get("negative_pnl_agent_count", 0),
                "positive_pnl_agent_count": run.accounting.get("positive_pnl_agent_count", 0),
                "zero_pnl_agent_count": run.accounting.get("zero_pnl_agent_count", 0),
                "max_open_orders": perf.get("max_open_orders", 0),
                "final_open_orders": perf.get("final_open_orders", 0),
                "max_abs_inventory_residual": inv.get("max_abs_inventory_residual", 0),
                "total_elapsed_ms": perf.get("total_elapsed_ms", 0),
                "actions_per_sec": perf.get("actions_per_sec", 0),
                "orders_per_sec": perf.get("orders_per_sec", 0),
            }
        )
    path = output / "plot_summary.csv"
    pd.DataFrame(rows).to_csv(path, index=False)
    print(path)


def main(argv: Optional[list[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--max-runs", type=int)
    parser.add_argument("--scenario")
    parser.add_argument("--seed")
    parser.add_argument("--downsample", type=int, default=1)
    parser.add_argument("--require-all", action="store_true")
    args = parser.parse_args(argv)

    global STRICT_REQUIRE_ALL
    STRICT_REQUIRE_ALL = args.require_all

    runs = discover_runs(args.input, args.scenario, args.seed, args.max_runs)
    args.output.mkdir(parents=True, exist_ok=True)
    print(f"Loaded {len(runs)} diagnostic run(s)")

    plot_price_paths(runs, args.output, args.downsample)
    plot_normalized_price_paths(runs, args.output, args.downsample)
    plot_returns(runs, args.output, args.downsample)
    plot_drawdown_spread_volume(runs, args.output, args.downsample)
    plot_actions(runs, args.output, args.downsample)
    plot_agents(runs, args.output, args.downsample)
    plot_accounting_book_events(runs, args.output, args.downsample)
    plot_extended_diagnostics(runs, args.output, args.downsample)
    write_plot_summary(runs, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
