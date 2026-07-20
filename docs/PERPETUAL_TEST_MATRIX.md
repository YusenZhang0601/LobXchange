# Perpetual Test Matrix

## Phase 1: Mark / Index / Unrealized PnL

Target: `lobx_perp_risk_tests`

- `PERP_MARK_001` set/get index price.
- `PERP_MARK_002` index price must be positive.
- `PERP_MARK_003` mark price uses last trade.
- `PERP_MARK_004` mark price uses mid price.
- `PERP_MARK_005` mark price uses index price.
- `PERP_MARK_006` long unrealized PnL.
- `PERP_MARK_007` short unrealized PnL.
- `PERP_MARK_008` full close unrealized PnL is zero.

## Phase 2: Maintenance Margin / Risk Tier

Target: `lobx_perp_risk_tests`

- `PERP_RISK_001` maintenance margin long.
- `PERP_RISK_002` maintenance margin short.
- `PERP_RISK_003` no position maintenance margin is zero.
- `PERP_RISK_004` notional hits configured tier.
- `PERP_RISK_005` large position max leverage is lower.
- `PERP_RISK_006` set leverage is clamped by tier.
- `PERP_RISK_007` order crossing tier uses new tier risk check.
- `PERP_RISK_008` rejected order has no side effects.
- `PERP_RISK_009` notional below first tier floor uses default risk.
- `PERP_RISK_010` notional exactly at tier floor matches tier.
- `PERP_RISK_011` notional exactly at capped boundary uses next tier.
- `PERP_RISK_012` notional above final open-ended floor matches final tier.

## Phase 3: Liquidation

Target: `lobx_perp_risk_tests`

- `PERP_LIQ_001` long price drop triggers liquidatable.
- `PERP_LIQ_002` short price rise triggers liquidatable.
- `PERP_LIQ_003` no position is not liquidatable.
- `PERP_LIQ_004` price recovery clears liquidatable.
- `PERP_LIQ_005` full liquidation clears position and releases margin.
- `PERP_LIQ_006` liquidation event recorded.
- `PERP_LIQ_007` liquidation settlement failure rolls back.
- `PERP_LIQ_008` bankruptcy price long.
- `PERP_LIQ_009` bankruptcy price short.
- `PERP_LIQ_010` healthy account liquidation rejected.
- `PERP_LIQ_011` liquidatable account liquidation succeeds.
- `PERP_LIQ_012` liquidation rejection has no side effects.

## Phase 4: Perpetual Fees

Target: `lobx_perp_fee_tests`

- `PERP_FEE_001` zero fee config no wallet mutation.
- `PERP_FEE_002` taker fee charged on open.
- `PERP_FEE_003` maker fee charged on fill.
- `PERP_FEE_004` fee reduces account equity.
- `PERP_FEE_005` fee can make account closer to liquidation.
- `PERP_FEE_006` fee accounting does not double-count realized PnL.
- `PERP_FEE_007` liquidation fee behavior deterministic.
- `PERP_FEE_008` fee rollback restores wallet, position, events, and fee totals.
- `PERP_FEE_009` rejected order has no fee side effects.
- `PERP_FEE_010` reduce-only fill charges configured fee.
- `PERP_FEE_011` fee event recorded.

## Phase 5: Funding

Target: `lobx_perp_funding_tests`

- `PERP_FUND_001` zero position no funding payment.
- `PERP_FUND_002` zero funding rate no wallet mutation.
- `PERP_FUND_003` positive funding long pays short receives.
- `PERP_FUND_004` negative funding short pays long receives.
- `PERP_FUND_005` funding changes account equity.
- `PERP_FUND_006` funding does not mutate unrealized PnL directly.
- `PERP_FUND_007` funding can make account liquidatable.
- `PERP_FUND_008` funding event emitted exactly once.
- `PERP_FUND_009` funding rollback restores wallet, totals, and events.
- `PERP_FUND_010` one-sided positive funding behavior pinned.
- `PERP_FUND_011` one-sided negative funding behavior pinned.
- `PERP_FUND_012` funding rate INT_MIN rejected.

## Phase 6: Perp simulate_fill

Target: `lobx_perp_simulate_fill_tests`

- `SIMF_PERP_001` long open dry-run estimates position and margin.
- `SIMF_PERP_002` short open dry-run estimates position and margin.
- `SIMF_PERP_003` long add dry-run averages entry.
- `SIMF_PERP_004` short add dry-run averages entry.
- `SIMF_PERP_005` long partial close dry-run realizes PnL and releases margin.
- `SIMF_PERP_006` short partial close dry-run realizes PnL and releases margin.
- `SIMF_PERP_007` reduce-only close has no required open margin.
- `SIMF_PERP_008` reduce-only increase rejects.
- `SIMF_PERP_009` insufficient margin rejects.
- `SIMF_PERP_010` post-only would-cross rejects.
- `SIMF_PERP_011` IOC partial fill has no residual rest.
- `SIMF_PERP_012` FOK partial fill rejects without estimated fills.
- `SIMF_PERP_013` dry-run does not mutate wallet, position, book, or events.
- `SIMF_PERP_014` fee estimate affects wallet delta.
- `SIMF_PERP_015` unrealized PnL after uses mark price.
- `SIMF_PERP_016` exchange API returns perp dry-run estimates.
- `SIMF_PERP_017` simulated fill list preserves price-time sweep.
- `SIMF_PERP_018` simulate FOK rejects partial fill.
- `SIMF_PERP_019` simulate STP prevents self-trade.
- `SIMF_PERP_020` simulated fee matches real submit.
- `SIMF_PERP_021` simulated position after matches real submit.
- `SIMF_PERP_022` simulated realized PnL matches real submit.
- `SIMF_PERP_023` simulated fill qty and notional match real submit.

## Phase 7: Insurance Fund / Bad Debt / ADL Placeholder

Target: `lobx_perp_insurance_adl_tests`

- `PERP_INS_001` credit insurance fund increases balance.
- `PERP_INS_002` reject non-positive insurance fund credit.
- `PERP_INS_003` insurance fund state restored on rollback.
- `PERP_INS_004` liquidation loss uses insurance fund.
- `PERP_INS_005` insurance fund insufficient records bad debt.
- `PERP_INS_006` bad debt query returns accumulated amount.
- `PERP_INS_007` liquidation event includes insurance paid and bad debt.
- `PERP_INS_008` rollback restores insurance fund and bad debt.
- `PERP_INS_009` insurance fund debit event recorded.
- `PERP_INS_010` bad debt event recorded.
- `PERP_ADL_001` insurance fund insufficient emits ADL_REQUIRED.
- `PERP_ADL_002` ADL candidates ranked deterministically.
- `PERP_ADL_003` high PnL ratio ranks before low PnL ratio.
- `PERP_ADL_004` high leverage tie-breaks PnL ratio.
- `PERP_ADL_005` account id deterministic tie-break.
- `PERP_ADL_006` no candidates when no profitable positions.

## Phase 8: Native Market Orders

Target: `lobx_perp_order_types_tests`

- `PERP_MKT_ORD_001` market buy consumes asks.
- `PERP_MKT_ORD_002` market sell consumes bids.
- `PERP_MKT_ORD_003` market order never rests.
- `PERP_MKT_ORD_004` market buy slippage protection rejects.
- `PERP_MKT_ORD_005` market sell slippage protection rejects.
- `PERP_MKT_ORD_006` market order requires protection price.
- `PERP_MKT_ORD_007` market order supports reduce-only.
- `PERP_MKT_ORD_008` market reduce-only cannot increase position.
- `PERP_MKT_ORD_009` market order supports STP.
- `PERP_MKT_ORD_010` market order charges taker fee.
- `PERP_MKT_ORD_011` failed market order rolls back all state.
- `PERP_MKT_ORD_012` market order emits event.

## Phase 9: Trigger Orders / TP / SL

Target: `lobx_perp_trigger_orders_tests`

- `PERP_TRG_001` create trigger order.
- `PERP_TRG_002` trigger order not in LOB before triggered.
- `PERP_TRG_003` cancel trigger order.
- `PERP_TRG_004` mark above triggers.
- `PERP_TRG_005` mark below triggers.
- `PERP_TRG_006` last above triggers.
- `PERP_TRG_007` last below triggers.
- `PERP_TRG_008` index above triggers.
- `PERP_TRG_009` index below triggers.
- `PERP_TRG_010` trigger fires once only.
- `PERP_TRG_011` trigger creates child market order.
- `PERP_TRG_012` trigger creates child limit order.
- `PERP_TRG_013` child order inherits reduce-only.
- `PERP_TRG_014` child order inherits STP.
- `PERP_TRG_015` child order failure records trigger failed.
- `PERP_TRG_016` cancel triggered order fails clearly.
- `PERP_TRG_017` trigger registry rollback-safe.
- `PERP_TRG_018` trigger events emitted.
- `PERP_TP_001` long take-profit sell reduce-only.
- `PERP_TP_002` short take-profit buy reduce-only.
- `PERP_SL_001` long stop-loss sell reduce-only.
- `PERP_SL_002` short stop-loss buy reduce-only.
- `PERP_TP_003` TP cannot increase position.
- `PERP_SL_003` SL cannot flip position.
- `PERP_TP_004` TP child order closes or reduces position.
- `PERP_SL_004` SL child order closes or reduces position.

## Deferred Phases

- Automatic ADL execution: not implemented.
- OCO TP/SL linkage: not implemented.
- Partial liquidation: not implemented.
- Oracle aggregation: not implemented.
- Production fee tiers / VIP levels: not implemented.
- Multi-collateral and cross-margin portfolio risk: not implemented.
