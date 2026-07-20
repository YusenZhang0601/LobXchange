# Price Impact Experiments

This experiment path uses the decoupled `AgentRuntime` with the implemented built-in agents only:

- `static_market_maker`
- `noise_trader`
- `momentum_follower`
- `mean_reverter`
- `whale_sweeper`

Future agent names such as `dynamic_market_maker`, `liquidity_taker`, `adversarial_sweeper`, and `liquidity_withdrawer` are not used or registered here.

## Smoke Tests

```bash
ctest --test-dir build-bench -R lobx_agent_price_impact_tests --output-on-failure
```

Smoke output defaults to:

```text
/tmp/lobx_price_impact_runs
```

Set a custom directory:

```bash
LOBX_PRICE_IMPACT_OUTPUT_DIR=build-bench/price_impact_runs \
ctest --test-dir build-bench -R lobx_agent_price_impact_tests --output-on-failure
```

## Large Experiments

Large scenarios are gated and do not run by default:

```bash
LOBX_RUN_LARGE_PRICE_IMPACT=1 \
LOBX_PRICE_IMPACT_OUTPUT_DIR=build-bench/price_impact_runs \
./build-bench/lobx_agent_price_impact_tests
```

Current long presets are `100 agents x 100 steps` per scenario. This is below the target `1000+ x 1000+` scale because the current `AgentRuntime` still stores simulation events and action traces in memory, market makers keep accumulating resting orders, and the loop remains step based.

## Outputs

Each run writes:

- `price_series.csv`
- `summary.json`
- `accounting_summary.json`

The root output directory also gets:

- `scenario_comparison.csv`

`price_series.csv` fields:

```text
step,ts,best_bid,best_ask,mid_price,spread,spread_bps,last_trade_price,cum_volume,trade_count
```

`summary.json` fields:

```text
scenario, seed, agent_count, steps, initial_mid_price, final_mid_price,
total_return_bps, realized_vol_bps, max_drawdown_bps,
largest_abs_return_bps, trade_count, cum_volume, price_samples_count
```

`accounting_summary.json` fields:

```text
sum_initial_equity, sum_final_equity, sum_agent_pnl,
exchange_fee_revenue, house_account_pnl, insurance_fund_pnl,
system_pnl_residual, negative_pnl_agent_count,
positive_pnl_agent_count, zero_pnl_agent_count, agent_count
```

The accounting summary uses total balances (`free + locked`) and mark-to-market inventory equity. In zero-fee closed-system runs, `system_pnl_residual` should be zero.

## Plotting

```bash
python scripts/plot_price_impact.py \
  --input build-bench/price_impact_runs \
  --output build-bench/price_impact_plots
```

The script writes PNG plots for price paths, normalized returns, return histograms, drawdown paths, and summary metric bars.
