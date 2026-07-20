#!/usr/bin/env python3
"""Plot LOBX price-impact experiment outputs.

Reads one run directory or a root directory containing many runs. A run is a
directory with both summary.json and price_series.csv.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, List

_CACHE_ROOT = Path(tempfile.gettempdir()) / "lobx_plot_price_impact_cache"
(_CACHE_ROOT / "matplotlib").mkdir(parents=True, exist_ok=True)
(_CACHE_ROOT / "xdg").mkdir(parents=True, exist_ok=True)
os.environ.setdefault("MPLCONFIGDIR", str(_CACHE_ROOT / "matplotlib"))
os.environ.setdefault("XDG_CACHE_HOME", str(_CACHE_ROOT / "xdg"))

try:
    import pandas as pd
except ImportError as exc:  # pragma: no cover - user environment dependent.
    raise SystemExit("Please install pandas matplotlib to plot price impact experiments.") from exc

try:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
except ImportError as exc:  # pragma: no cover - user environment dependent.
    raise SystemExit("Please install pandas matplotlib to plot price impact experiments.") from exc


@dataclass
class RunData:
    run_dir: Path
    summary: dict
    prices: pd.DataFrame

    @property
    def label(self) -> str:
        scenario = str(self.summary.get("scenario", self.run_dir.parent.name))
        seed = self.summary.get("seed")
        return f"{scenario}/seed={seed}" if seed is not None else scenario


def discover_runs(input_path: Path) -> List[Path]:
    if not input_path.exists():
        raise SystemExit(f"Input path does not exist: {input_path}")

    if input_path.is_file():
        if input_path.name != "summary.json":
            raise SystemExit(f"Input file must be summary.json: {input_path}")
        run_dirs = [input_path.parent]
    elif (input_path / "summary.json").exists():
        run_dirs = [input_path]
    else:
        run_dirs = sorted(path.parent for path in input_path.rglob("summary.json"))

    if not run_dirs:
        raise SystemExit(f"No summary.json files found under: {input_path}")
    return run_dirs


def load_run(run_dir: Path) -> RunData:
    summary_path = run_dir / "summary.json"
    price_path = run_dir / "price_series.csv"
    if not summary_path.exists():
        raise SystemExit(f"Missing summary.json in run directory: {run_dir}")
    if not price_path.exists():
        raise SystemExit(f"Missing price_series.csv next to: {summary_path}")

    with summary_path.open("r", encoding="utf-8") as fh:
        summary = json.load(fh)

    prices = pd.read_csv(price_path)
    if prices.empty:
        raise SystemExit(f"price_series.csv is empty: {price_path}")
    if "mid_price" not in prices.columns:
        raise SystemExit(f"price_series.csv missing mid_price column: {price_path}")

    prices = prices.copy()
    prices["mid_price"] = pd.to_numeric(prices["mid_price"], errors="coerce")
    prices = prices[prices["mid_price"].notna() & (prices["mid_price"] > 0)]
    if prices.empty:
        raise SystemExit(f"price_series.csv has no valid positive mid_price values: {price_path}")

    if "step" not in prices.columns:
        prices["step"] = range(len(prices))
    prices["step_return_bps"] = prices["mid_price"].pct_change().fillna(0.0) * 10000.0
    running_peak = prices["mid_price"].cummax()
    prices["drawdown_bps"] = (prices["mid_price"] / running_peak - 1.0) * 10000.0
    initial_mid = prices["mid_price"].iloc[0]
    prices["normalized_return_bps"] = (prices["mid_price"] / initial_mid - 1.0) * 10000.0
    return RunData(run_dir=run_dir, summary=summary, prices=prices)


def load_runs(input_path: Path) -> List[RunData]:
    return [load_run(run_dir) for run_dir in discover_runs(input_path)]


def savefig(output_dir: Path, filename: str) -> Path:
    path = output_dir / filename
    plt.tight_layout()
    plt.savefig(path, dpi=160)
    plt.close()
    print(path)
    return path


def plot_price_paths(runs: Iterable[RunData], output_dir: Path) -> Path:
    plt.figure(figsize=(11, 6))
    for run in runs:
        plt.plot(run.prices["step"], run.prices["mid_price"], linewidth=1.4, label=run.label)
    plt.xlabel("step")
    plt.ylabel("mid_price")
    plt.title("Price paths")
    plt.legend(fontsize=8)
    plt.grid(True, alpha=0.25)
    return savefig(output_dir, "price_paths.png")


def plot_normalized_price_paths(runs: Iterable[RunData], output_dir: Path) -> Path:
    plt.figure(figsize=(11, 6))
    for run in runs:
        plt.plot(run.prices["step"], run.prices["normalized_return_bps"], linewidth=1.4, label=run.label)
    plt.xlabel("step")
    plt.ylabel("mid_price / initial_mid - 1 (bps)")
    plt.title("Normalized price paths")
    plt.legend(fontsize=8)
    plt.grid(True, alpha=0.25)
    return savefig(output_dir, "normalized_price_paths.png")


def plot_returns_histogram(runs: Iterable[RunData], output_dir: Path) -> Path:
    plt.figure(figsize=(11, 6))
    for run in runs:
        values = run.prices["step_return_bps"].iloc[1:]
        if not values.empty:
            plt.hist(values, bins=40, alpha=0.35, label=run.label)
    plt.xlabel("step_return_bps")
    plt.ylabel("count")
    plt.title("Step return distribution")
    plt.legend(fontsize=8)
    plt.grid(True, alpha=0.25)
    return savefig(output_dir, "returns_histogram.png")


def plot_drawdown_paths(runs: Iterable[RunData], output_dir: Path) -> Path:
    plt.figure(figsize=(11, 6))
    for run in runs:
        plt.plot(run.prices["step"], run.prices["drawdown_bps"], linewidth=1.4, label=run.label)
    plt.xlabel("step")
    plt.ylabel("drawdown (bps)")
    plt.title("Drawdown paths")
    plt.legend(fontsize=8)
    plt.grid(True, alpha=0.25)
    return savefig(output_dir, "drawdown_paths.png")


def plot_summary_bar(runs: List[RunData], output_dir: Path) -> Path:
    metrics = [
        "total_return_bps",
        "realized_vol_bps",
        "max_drawdown_bps",
        "largest_abs_return_bps",
    ]
    labels = [run.label for run in runs]
    x = range(len(labels))

    fig, axes = plt.subplots(2, 2, figsize=(13, 8))
    for ax, metric in zip(axes.flatten(), metrics):
        values = [float(run.summary.get(metric, 0.0) or 0.0) for run in runs]
        ax.bar(x, values)
        ax.set_title(metric)
        ax.set_xticks(list(x))
        ax.set_xticklabels(labels, rotation=35, ha="right", fontsize=8)
        ax.grid(True, axis="y", alpha=0.25)
    fig.suptitle("Scenario summary metrics")
    return savefig(output_dir, "scenario_summary_bar.png")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=Path, help="Run directory or root containing run directories")
    parser.add_argument("--output", required=True, type=Path, help="Directory for PNG plots")
    args = parser.parse_args(argv)

    runs = load_runs(args.input)
    args.output.mkdir(parents=True, exist_ok=True)
    print(f"Loaded {len(runs)} run(s). Saving plots to {args.output}")

    plot_price_paths(runs, args.output)
    plot_normalized_price_paths(runs, args.output)
    plot_returns_histogram(runs, args.output)
    plot_drawdown_paths(runs, args.output)
    plot_summary_bar(runs, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
