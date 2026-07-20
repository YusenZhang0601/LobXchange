#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys
from typing import Any


LOWER_IS_BAD = {
    "orders_per_sec": 0.15,
    "steps_per_sec": 0.15,
    "trades_per_sec": 0.15,
}

HIGHER_IS_BAD = {
    "submit_latency_p95_ns": 0.20,
    "decision_latency_p95_ns": 0.20,
    "snapshot_latency_p95_us": 0.20,
    "json_latency_p95_us": 0.20,
    "rss_mb": 0.20,
}

FAIL_MULTIPLIER = {
    "orders_per_sec": 0.30,
    "steps_per_sec": 0.30,
    "trades_per_sec": 0.30,
    "submit_latency_p95_ns": 0.50,
    "decision_latency_p95_ns": 0.50,
    "snapshot_latency_p95_us": 0.50,
    "json_latency_p95_us": 0.50,
    "rss_mb": 0.50,
}


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def load_inputs(path: Path) -> dict[str, dict[str, Any]]:
    if path.is_file():
        return {path.name: load_json(path)}
    data: dict[str, dict[str, Any]] = {}
    for item in sorted(path.glob("*.json")):
        data[item.name] = load_json(item)
    if not data:
        raise SystemExit(f"no JSON benchmark files found in {path}")
    return data


def ratio_drop(current: float, baseline: float) -> float:
    if baseline <= 0:
        return 0.0
    return (baseline - current) / baseline


def ratio_increase(current: float, baseline: float) -> float:
    if baseline <= 0:
        return 0.0
    return (current - baseline) / baseline


def compare_one(name: str, baseline: dict[str, Any], current: dict[str, Any], mode: str) -> list[str]:
    messages: list[str] = []
    lower_thresholds = dict(LOWER_IS_BAD)
    higher_thresholds = dict(HIGHER_IS_BAD)
    if mode == "fail":
        lower_thresholds = {k: FAIL_MULTIPLIER[k] for k in LOWER_IS_BAD}
        higher_thresholds = {k: FAIL_MULTIPLIER[k] for k in HIGHER_IS_BAD}

    for key, threshold in lower_thresholds.items():
        if key not in baseline or key not in current:
            continue
        drop = ratio_drop(float(current[key]), float(baseline[key]))
        if drop > threshold:
            messages.append(
                f"{name}: {key} dropped {drop:.1%} baseline={baseline[key]} current={current[key]}"
            )

    for key, threshold in higher_thresholds.items():
        if key not in baseline or key not in current:
            continue
        increase = ratio_increase(float(current[key]), float(baseline[key]))
        if increase > threshold:
            messages.append(
                f"{name}: {key} increased {increase:.1%} baseline={baseline[key]} current={current[key]}"
            )

    for key in ("accepted_orders", "rejected_orders", "trade_count"):
        if key in baseline and key in current and baseline.get("case") == current.get("case") and baseline[key] != current[key]:
            messages.append(f"{name}: golden {key} changed baseline={baseline[key]} current={current[key]}")
    return messages


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Compare C++ benchmark JSON output with a baseline.")
    parser.add_argument("--baseline", required=True, type=Path)
    parser.add_argument("--current", required=True, type=Path)
    parser.add_argument("--mode", choices=("record", "warning", "fail"), default="record")
    args = parser.parse_args(argv)

    baseline = load_inputs(args.baseline)
    current = load_inputs(args.current)
    messages: list[str] = []
    for name, current_obj in current.items():
        base_obj = baseline.get(name)
        if base_obj is None:
            messages.append(f"{name}: no matching baseline")
            continue
        messages.extend(compare_one(name, base_obj, current_obj, args.mode))

    if not messages:
        print("benchmark comparison: ok")
        return 0

    prefix = "WARNING" if args.mode != "fail" else "FAIL"
    for message in messages:
        print(f"{prefix}: {message}")
    return 1 if args.mode == "fail" else 0


if __name__ == "__main__":
    raise SystemExit(main())
