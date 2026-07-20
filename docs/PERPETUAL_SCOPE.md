# LOBX Perpetual Scope

This project models local perpetual futures for simulation and research. It is not a production derivatives exchange.

## Supported

- Perpetual market creation with margin asset and max leverage.
- Per-user leverage, clamped by market max leverage and configured risk tiers.
- Limit order matching through the embedded LOB engine.
- IOC/FOK/POST_ONLY/STP plus `LOBX_REDUCE_ONLY`.
- Native protected market orders for perpetuals, internally routed as IOC limit orders with mandatory slippage protection.
- Minimal trigger order registry for mark/last/index triggers with market or limit child orders.
- Take-profit and stop-loss semantics expressible as reduce-only trigger orders.
- Long/short positions with average entry price and realized PnL.
- Position margin lock/release during open, partial close, and full close.
- Index price storage for perpetual markets.
- Mark price modes:
  - `LastTrade`
  - `MidPrice`
  - `IndexPrice`
- Unrealized PnL query from mark price.
- Maintenance margin query from mark notional and configured risk tier bps.
- Liquidatable detection using account equity and maintenance margin.
- Basic full liquidation utility for liquidatable accounts that closes a position at mark price, releases margin, records a liquidation event, and rolls back on settlement failure.
- Perpetual maker/taker fee config, charged from the margin wallet on committed fills.
- Deterministic liquidation fee config, charged from the liquidated account in the simplified full-liquidation path.
- Per-account fee total query.
- Basic funding rate settlement at mark price, with long/short pay/receive semantics and per-account funding total query.
- Perp `simulate_fill` dry-run for limit/IOC/FOK/POST_ONLY/STP/reduce-only paths, including estimated fills, notional, fee, margin, realized PnL, unrealized PnL, and position after the simulated taker order.
- Market-level insurance fund balance denominated in the perpetual market's margin asset.
- Market-level bad debt recording for liquidation loss not covered by account margin or the insurance fund.
- ADL placeholder event and deterministic candidate ranking for profitable positions; no automatic deleveraging is executed.
- Simplified bankruptcy price query for diagnostics.

## Simplified Model

- Margin is single-asset margin per perpetual market engine.
- Account equity is `margin wallet total + unrealized_pnl`. Realized PnL is not added again because realized PnL settlement already credits or withdraws wallet balance.
- Maintenance margin is tier bps times mark-price notional.
- Fees are quote/margin-asset fees. The current model supports symbol-level bps config only.
- Funding is direct wallet settlement from mark notional and configured funding bps. It is not a premium-index/oracle funding engine.
- Liquidation is full-position close at mark price. Loss is covered by liquidated account margin first, then the market insurance fund, then market bad debt. There is no partial liquidation ladder yet.
- Insurance fund and bad debt are scoped to a single `MarketEngine`; the balance/debt is denominated in that market's margin asset and is not shared across markets.
- Market orders never rest. A buy market order requires a max protection price; a sell market order requires a min protection price.
- Trigger orders are stored outside the ordinary LOB until fired. Trigger evaluation is explicit in the local simulation API.
- Bankruptcy price uses retained position margin only and is intended for tests/logging, not production liquidation routing.

## Not Yet Implemented

- Automatic ADL execution.
- OCO take-profit/stop-loss linkage.
- Production-grade fee tiers, VIP levels, rebates, and fee schedules.
- Premium-index funding, oracle aggregation, and production funding clamps.
- Mark/index oracle aggregation.
- Multi-collateral and cross-margin portfolio risk.
- Risk limits beyond simple notional tiers.
