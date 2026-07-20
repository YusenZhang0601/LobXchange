# Long Diagnostic Experiments

Long diagnostic experiments run the decoupled `AgentRuntime` for many steps and write a research bundle for manual accounting, market microstructure, and visualization checks. They are not part of ordinary CI unless explicitly enabled.

## Run

Default long diagnostic:

```bash
LOBX_RUN_LONG_DIAGNOSTIC=1 \
LOBX_DIAG_OUTPUT_DIR=build-bench/diagnostic_runs \
./build-bench/lobx_agent_long_diagnostic_tests
```

Smaller smoke run:

```bash
LOBX_RUN_LONG_DIAGNOSTIC=1 \
LOBX_DIAG_AGENT_COUNT=20 \
LOBX_DIAG_STEPS=1000 \
LOBX_DIAG_SAMPLE_INTERVAL=10 \
LOBX_DIAG_OUTPUT_DIR=build-bench/diagnostic_runs_phase_a \
./build-bench/lobx_agent_long_diagnostic_tests
```

Bounded quoting/cadence smoke:

```bash
LOBX_RUN_LONG_DIAGNOSTIC=1 \
LOBX_DIAG_SCENARIOS=bounded \
LOBX_DIAG_AGENT_COUNT=20 \
LOBX_DIAG_STEPS=5000 \
LOBX_DIAG_SAMPLE_INTERVAL=10 \
LOBX_DIAG_OUTPUT_DIR=build-bench/diagnostic_runs_bounded \
./build-bench/lobx_agent_long_diagnostic_tests
```

Adjust scale:

```bash
LOBX_RUN_LONG_DIAGNOSTIC=1 \
LOBX_DIAG_AGENT_COUNT=80 \
LOBX_DIAG_STEPS=100000 \
LOBX_DIAG_SAMPLE_INTERVAL=20 \
LOBX_DIAG_BOOK_DEPTH=10 \
LOBX_DIAG_OUTPUT_DIR=build-bench/diagnostic_runs \
./build-bench/lobx_agent_long_diagnostic_tests
```

Enable verbose JSONL:

```bash
LOBX_RUN_LONG_DIAGNOSTIC=1 \
LOBX_DIAG_ENABLE_JSONL_EVENTS=1 \
LOBX_DIAG_OUTPUT_DIR=build-bench/diagnostic_runs_verbose \
./build-bench/lobx_agent_long_diagnostic_tests
```

## Scenarios

- `balanced_mixed_agents_long`: 40% static market maker, 30% noise, 15% momentum, 10% mean reverter, 5% whale sweeper.
- `stress_whale_momentum_long`: 35% static market maker, 25% noise, 25% momentum, 5% mean reverter, 10% whale sweeper.
- `balanced_mixed_agents_bounded_long`: same mix as balanced, with bounded static quoting and per-type decision cadence.
- `stress_whale_momentum_bounded_long`: same mix as stress, with bounded static quoting and per-type decision cadence.
- `market_maker_inventory_pressure_long`: helper scenario for future focused runs; 70% static market maker, 30% noise.

`LOBX_DIAG_SCENARIOS=legacy|bounded|all` selects which scenario family the long test runs. The default is `legacy` to preserve prior diagnostic behavior.

Only implemented built-ins are used. Future agent types are not registered or mapped to fallback strategies.

## Output Bundle

Each run writes to:

```text
<output_root>/<scenario>/seed=<seed>/
```

Files:

- `run_metadata.json`: scenario, seed, scale, sampling, output switches, build metadata placeholders.
- `summary.json`: price path metrics.
- `accounting_summary.json`: total-equity accounting residual and aggregate balances.
- `price_series.csv`: sampled prices, spread, volume, returns, drawdown.
- `agent_state_samples.csv`: sampled per-agent total-equity state.
- `agent_final_state.csv`: final per-agent total-equity state.
- `agent_type_summary.csv`: PnL and inventory aggregation by implemented agent type.
- `agent_type_pnl_timeseries.csv`: sampled mark-to-market PnL aggregation by agent type.
- `inventory_consistency_by_agent.csv`: final inventory reconciliation: initial inventory plus buys minus sells.
- `inventory_consistency_summary.json`: max/mean inventory residual and failed agent count.
- `open_order_growth.csv`: sampled total, bid, ask, per-agent, and stale open-order counts.
- `actions_by_step.csv`: per-step action counts.
- `actions_by_type.csv`: per-step x agent type action counts.
- `trades_by_step.csv`: per-step trade counts, volume, aggressor volume, VWAP.
- `book_samples.csv`: sampled top-N bid/ask book depth in long format.
- `runtime_metrics.csv`: elapsed time, interval phase timings, cumulative actions/orders/trades/rejections/open orders, and scheduler due/skip counts.
- `perf_summary.json`: total runtime, phase timing totals, throughput, and max/final open orders.
- `run_hash.json`: deterministic hashes for price series, final agent state, accounting summary, and inventory consistency.
- `unit_sanity_summary.json`: quantity/inventory/cash scale sanity diagnostics.
- `diagnostic_warnings.json`: unavailable or disabled diagnostics.

Optional JSONL files are header-only or empty unless enabled:

- `agent_actions.jsonl`
- `orders.jsonl`
- `trades.jsonl`
- `cancels.jsonl`
- `rejections.jsonl`
- `simulation_events.jsonl`
- `impact_windows.csv`

## Plot

```bash
python scripts/plot_diagnostic_bundle.py \
  --input build-bench/diagnostic_runs \
  --output build-bench/diagnostic_plots \
  --downsample 5
```

Filters:

```bash
python scripts/plot_diagnostic_bundle.py \
  --input build-bench/diagnostic_runs \
  --output build-bench/diagnostic_plots \
  --scenario balanced_mixed_agents_long \
  --downsample 10
```

The plotting script writes PNGs for price paths, normalized prices, returns, drawdown, spread, volume/trades, action counts, agent PnL distribution, PnL by type, equity and inventory by type, locked balances, accounting residual, book depth, event rate, and optional impact windows. It also writes `plot_summary.csv`.

New diagnostic plots include:

- `open_order_growth.png`: identifies linear stale-order growth; bounded runs should plateau.
- `mean_pnl_by_type_timeseries.png`: mark-to-market PnL by agent type over time.
- `inventory_consistency_residual.png`: should be exactly zero for spot closed-system accounting.
- `runtime_phase_breakdown.png`: interval timing for decide, schedule, exchange apply, recorder, and book sampling.
- `pnl_vs_inventory_scatter.png`: final PnL versus inventory exposure by agent type.
- `locked_balance_by_type.png`: cash/inventory locked by type over time.
- `action_to_trade_funnel.png`: total actions by type; trade-by-type fill rates are not yet available.
- `perf_summary_bar.png`: compares elapsed time, phase totals, max open orders, and throughput across runs.

Use `--require-all` to make missing optional diagnostic files fail plotting instead of warning.

## Interpreting PnL

To diagnose “all agents are losing”:

1. Check `accounting_summary.json` first. In zero-fee spot closed-system runs, `system_pnl_residual` should be zero.
2. Use `agent_final_state.csv.total_pnl`, not cash-only PnL.
3. Use total balances (`free + locked`), not available balances.
4. Inspect `locked_balance_paths.png` to see whether open orders are locking cash/inventory.
5. Check `inventory_consistency_summary.json`; residuals should be zero.
6. If aggregate negative PnL appears with nonzero fees, include `exchange_fee_revenue`.

## Interpreting Performance

- If `open_order_growth.png` grows linearly in legacy runs, stale static quotes are dominating book/open-order state.
- Bounded runs should cap per-agent open orders through cancel + refresh actions.
- `runtime_phase_breakdown.png` separates agent decision, action scheduling, exchange apply, recorder, and book sampling time.
- `perf_summary.json.max_open_orders` and `final_open_orders` are the fastest way to compare legacy versus bounded runs.
- Bounded quoting changes strategy behavior frequency and quote lifetime, so legacy and bounded runs are performance/stability comparisons, not identical economic experiments.

## Limits

- Accounting diagnostics are spot-only in this path.
- Mark price policy is currently `mid_price`.
- House, insurance, perp funding, and liquidation accounting are placeholders in this diagnostic bundle.
- Smaller `LOBX_DIAG_SAMPLE_INTERVAL` and enabled JSONL can produce large files.
- These experiments are diagnostic; do not use them as strict evidence that a simple strategy should make or lose money.
