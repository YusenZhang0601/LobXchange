# LOBX Perpetual Risk Model

## Mark Price

`MarkPriceMode` is defined in `cpp/include/lobx/types.hpp`.

- `LastTrade`: last committed perpetual trade price. If no trade exists, the engine falls back to mid, then index, then single-sided book price.
- `MidPrice`: `(best_bid + best_ask) / 2` when both sides exist. If one side is missing, the engine falls back to last trade, then index, then the available side.
- `IndexPrice`: externally set index price. If unset, the engine falls back instead of returning an invalid negative value.

Index price is stored per `MarketEngine` and is valid only for `MarketType::Perpetual`. It must be positive.

## Unrealized PnL

Unrealized PnL is a query-only value. It does not change `Position::realized_pnl`.

```text
long  = (mark_price - entry_price) * abs(qty)
short = (entry_price - mark_price) * abs(qty)
```

Full close sets position quantity to zero, so unrealized PnL returns zero.

## Risk Tiers

`PerpRiskTier` uses integer notional and bps fields:

```cpp
struct PerpRiskTier {
  Amount notional_floor;
  Amount notional_cap;              // 0 means open-ended
  int initial_margin_bps;
  int maintenance_margin_bps;
  int max_leverage;
};
```

Tier selection uses mark notional for maintenance queries and projected order notional for submit-time leverage clamp. If no tiers are configured, the engine keeps the legacy `market.max_leverage` behavior.

When both `market.max_leverage` and `tier.max_leverage` apply, the stricter value is used.

## Maintenance Margin

```text
maintenance_margin = abs(position_qty) * mark_price * maintenance_margin_bps / 10000
```

The result is rounded up. Users without a position have maintenance margin zero.

## Account Equity

```text
equity = margin_wallet_total + unrealized_pnl
```

`margin_wallet_total` includes free and locked margin. Realized PnL is already reflected in wallet balance by settlement, so it is not added a second time.

## Fee Accounting

`PerpFeeConfig` is symbol-level:

```cpp
struct PerpFeeConfig {
  int maker_fee_bps;
  int taker_fee_bps;
  int liquidation_fee_bps;
};
```

Committed perpetual fills charge the resting order side `maker_fee_bps` and the aggressive order side `taker_fee_bps`.

```text
fee = abs(fill_qty) * fill_price * fee_bps / 10000
```

Fees are charged from the margin wallet in the market margin asset and are accumulated in `account_fee_total`. Fees reduce wallet balance and therefore reduce account equity. Realized PnL settlement remains separate, so fees are not added to or subtracted from `Position::realized_pnl`.

Each non-zero fee emits `perp.fee_charged` after successful settlement. Rejected or rolled-back fills do not emit fee events and do not leave fee totals behind.

Liquidation fee uses `liquidation_fee_bps` in the simplified full-liquidation path. The fee is charged only after liquidation settlement checks pass and is rolled back with the rest of liquidation state on failure.

Production gaps: no maker rebates, VIP tiers, volume tiers, fee schedules, or multi-asset fee currency selection.

## Perp simulate_fill

`MarketEngine::simulate_fill` and `Exchange::simulate_fill` are dry-run queries. For perpetual markets they scan the current opposite book and estimate the aggressive order's accepted/rejected status, filled quantity, notional, taker fee, required retained margin, realized PnL, unrealized PnL after the simulated position, wallet delta, margin delta, position quantity/entry before and after, and individual simulated fill legs.

Supported perp paths include long/short open, long/short add, partial close, reduce-only close, IOC, FOK, POST_ONLY would-cross rejection, STP skipping, and insufficient-margin rejection. The query does not commit order accepted/rejected events, trades, fee events, funding events, order ids, resting orders, wallet balances, locked margin, positions, entry price, or realized PnL.

Targeted coverage pins FOK partial-liquidity dry-runs as rejected with no estimated fills. STP dry-runs skip same-account resting liquidity; if that leaves no external liquidity, the IOC dry-run is accepted as a no-fill preview with `self_liquidity_skipped` populated and no residual rest.

Simulate-vs-real-submit tests compare estimated fee, position quantity, entry price, realized PnL, wallet delta, margin delta, filled quantity, and filled notional against a real submit from an identical initial fixture. The perp benchmark mixed workload also records both accepted and rejected dry-run calls through `simulate_fill_calls`, `simulate_fill_accepts`, and `simulate_fill_rejects`.

Production gaps: this is still a limit-order dry-run; it is not a native market-order API, trigger-order preview, liquidation preview, funding preview, or cross-margin portfolio simulator.

## Market and Trigger Orders

Perpetual native market orders are implemented as protected IOC limit orders. Buy market orders require a maximum executable price. Sell market orders require a minimum executable price. If the protection price leaves no executable external liquidity, the market order is rejected before the mutating submit path. Market orders never rest and reuse the existing IOC settlement path for margin, positions, fees, reduce-only, STP, rollback, and trade events. Accepted market orders also emit `order.market`.

Trigger orders are held in a market-level registry outside the ordinary LOB. They do not affect book priority or lock order margin before firing. A trigger has a price source (`Mark`, `Last`, or `Index`), condition (`AboveOrEqual` or `BelowOrEqual`), child order type (`Market` or `Limit`), child prices/protection, quantity, side, and inherited flags such as reduce-only and STP.

Trigger evaluation is explicit in the local simulation API. When a trigger fires, its child order is submitted through the existing market or limit submit path. Successful child submission marks the trigger `Triggered` and emits `trigger.fired` plus `trigger.child_order`; child rejection marks it `Failed` and emits `trigger.failed`. Active trigger cancellation emits `trigger.cancelled`. Trigger registry state participates in `MarketEngine` snapshots, so failed child settlement restores book, wallet, position, and event state through the existing submit rollback path.

Take-profit and stop-loss are represented as reduce-only trigger order patterns:

- Long take-profit: sell reduce-only when price is above or equal to target.
- Long stop-loss: sell reduce-only when price is below or equal to stop.
- Short take-profit: buy reduce-only when price is below or equal to target.
- Short stop-loss: buy reduce-only when price is above or equal to stop.

OCO pairing between TP and SL is not implemented.

## Funding

Funding is a direct wallet settlement:

```text
payment = abs(position_qty) * mark_price * abs(funding_rate_bps) / 10000
```

Positive funding means longs pay and shorts receive. Negative funding means shorts pay and longs receive. Funding updates wallet balance and `account_funding_total`; it does not change unrealized PnL or `Position::realized_pnl`.

`account_funding_total` is signed: positive means the account received net funding, negative means it paid net funding.

Funding settlement emits per-account `funding.payment` events plus one aggregate `funding.settled` event after successful settlement. If a payer cannot pay, ledger state, funding totals, and events are restored.

This is a simplified per-account funding model. It does not match long/short pools and does not enforce full-market zero-sum funding. One-sided positions may create a local balance source or sink. Production gaps: no premium index, oracle aggregation, funding clamp model, external insurance/settlement account, or scheduled production funding engine.

## Liquidation

Current detection:

```text
liquidatable = position exists && account_equity <= maintenance_margin
```

Current execution is deliberately minimal:

- Reject liquidation if the account is not currently liquidatable.
- Close the full position at mark price.
- Release user order locks and position margin.
- Apply positive realized PnL to the margin wallet.
- For realized losses, cover loss from liquidated account free margin first, then the market insurance fund, then record uncovered residual as market bad debt.
- Apply configured liquidation fee, if any.
- Clear the position through `PositionEngine`.
- Emit insurance-fund debit, bad-debt, `ADL_REQUIRED`, fee, and `liquidation` events only after successful settlement.
- Restore ledger, book/open orders, positions, position margin, insurance fund, bad debt, and uncommitted events on failure.

Insurance fund and bad debt are market-level state on `MarketEngine`, denominated in the market margin asset. They are not shared by asset across multiple markets.

Loss waterfall:

```text
liquidated account free margin after position margin release
  -> market insurance fund
  -> market bad debt
  -> ADL_REQUIRED event
```

Insurance fund credits must be positive and emit `insurance_fund.credited`. Liquidation loss debits emit `insurance_fund.debited` with the market id, margin asset, amount, balance after, and reason. Bad debt emits `perp.bad_debt_recorded` with market id, margin asset, account id, amount, total bad debt, and reason. The final `liquidation` event includes account id, market id, position quantity, mark/liquidation price, loss, account loss paid, insurance paid, bad debt, and fee.

ADL is a placeholder. `ADL_REQUIRED` is emitted when liquidation leaves bad debt after the insurance fund is exhausted. No other accounts are reduced automatically. `rank_adl_candidates` returns profitable positions ranked by higher unrealized-PnL ratio, then higher effective leverage, then lower account id as a deterministic tie-breaker. Candidates include account id, signed quantity, mark notional, unrealized PnL, PnL ratio, effective leverage, and rank.

Production gaps: no automatic ADL execution, no insurance-fund governance or external custody, no partial liquidation, no external liquidation order book.
