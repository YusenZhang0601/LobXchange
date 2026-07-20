# Perp TDD Roadmap

This roadmap separates implemented correctness tests from pending specification tests. Do not add pending specs to default correctness until the target feature is under implementation and the tests can be made green.

## Test Layers

1. Implemented correctness tests
   - Included in `scripts/run_cpp_correctness.sh`.
   - Must stay green.

2. Pending/spec tests
   - May live under `cpp/tests/perp/`.
   - Must be guarded by `LOBX_ENABLE_PENDING_PERP_TESTS`, `DISABLED_`, or `TODO_` naming.
   - Must not be wired into default CMake targets.

3. Feature branch tests
   - Enable one pending group when implementing that feature.
   - Promote to default correctness only after implementation is green.

## Suggested Pending Directory

```text
cpp/tests/perp/
  perp_oracle_mark_tests.cpp
  perp_margin_model_tests.cpp
  perp_funding_engine_tests.cpp
  perp_liquidation_waterfall_tests.cpp
  perp_insurance_adl_tests.cpp
  perp_order_types_tests.cpp
  perp_trigger_orders_tests.cpp
  perp_cross_margin_tests.cpp
  perp_audit_replay_tests.cpp
  perp_risk_hot_reload_tests.cpp
  perp_property_invariants_tests.cpp
```

## Always-On Invariants

Target file: `perp_property_invariants_tests.cpp`

- `PERP-INV-001` balances non-negative unless explicit bad debt exists.
- `PERP-INV-002` wallet, insurance, fee sink, and bad debt accounting reconciles.
- `PERP-INV-003` position signed quantity and side are consistent.
- `PERP-INV-004` full close leaves zero quantity and zero unrealized PnL.
- `PERP-INV-005` reduce-only never increases absolute position.
- `PERP-INV-006` reduce-only never flips position.
- `PERP-INV-007` through `PERP-INV-015` rejected/failed settlement paths have no uncommitted mutation.
- `PERP-INV-016` best bid is never above best ask.
- `PERP-INV-017` IOC residual never rests.
- `PERP-INV-018` POST_ONLY never actively crosses.
- `PERP-INV-019` FOK never partially fills.
- `PERP-INV-020` STP never creates self trade.

## Phase A: Risk Closure

Status: largely active for local simulation.

- Mark/index price and unrealized PnL.
- Maintenance margin and risk tiers.
- Full liquidation.
- Insurance fund.
- Bad debt.
- `ADL_REQUIRED` event.
- ADL candidate ranking.
- Liquidation waterfall tests.

Pending extensions:
- Stale mark/index liquidation protection.
- Partial liquidation ladder.
- Bankruptcy-price production semantics.
- Automatic ADL execution.

## Phase B: Order System

Target file: `perp_order_types_tests.cpp`

Pending groups:
- Native market orders: `PERP-MKT-ORD-001` through `PERP-MKT-ORD-010`.
- Amend order: `PERP-AMEND-001` through `PERP-AMEND-008`.
- Batch cancel / cancel all: `PERP-CANCEL-001` through `PERP-CANCEL-008`.
- Order expiry: `PERP-EXP-001` through `PERP-EXP-006`.

Do not start trigger orders in this phase unless the native order surface is stable.

## Phase C: Funding Engine

Target file: `perp_funding_engine_tests.cpp`

Implemented simplified model remains pinned:
- `PERP-FUND-SIMPLE-001` through `PERP-FUND-SIMPLE-008`.

Pending production model:
- Peer-to-peer zero-sum funding: `PERP-FUND-P2P-001` through `PERP-FUND-P2P-010`.
- Funding scheduler: `PERP-FUND-SCHED-001` through `PERP-FUND-SCHED-008`.
- Premium index funding: `PERP-FUND-PREM-001` through `PERP-FUND-PREM-007`.

## Phase D: Margin Productization

Target file: `perp_margin_model_tests.cpp`

Pending groups:
- Isolated margin: `PERP-ISO-001` through `PERP-ISO-010`.
- Cross margin: `PERP-CROSS-001` through `PERP-CROSS-010`.
- Risk tier hot reload and validation: `PERP-TIER-001` through `PERP-TIER-013`.

## Phase E: Oracle, Replay, Persistence

Targets:
- `perp_oracle_mark_tests.cpp`
- `perp_audit_replay_tests.cpp`
- `perp_snapshot_persistence_tests.cpp`

Pending groups:
- Oracle/index aggregation: `PERP-ORACLE-001` through `PERP-ORACLE-010`.
- Mark/premium price model: `PERP-MARK-001` through `PERP-MARK-010`, `PERP-PREMIUM-001` through `PERP-PREMIUM-005`.
- Event audit fields: `PERP-EVT-001` through `PERP-EVT-011`.
- Replay reconstruction: `PERP-REPLAY-001` through `PERP-REPLAY-010`.
- Snapshot restore/serialization: `PERP-SNAP-001` through `PERP-SNAP-012`.

## Trigger Orders And API Hardening

Keep these pending until the base order system is stable:

- Trigger registry and conditions: `PERP-TRG-001` through `PERP-TRG-COND-008`.
- Trigger child orders: `PERP-TRG-CHILD-001` through `PERP-TRG-CHILD-006`.
- TP/SL/OCO: `PERP-TP-001` through `PERP-SL-004`.
- API permissions/rate limits: `PERP-API-001` through `PERP-API-010`.

## Benchmark Roadmap

Current `lobx_bench_perp` should remain parseable JSON. Expand from the mixed workload into separate cases over time:

- `BM-PERP-001` long open.
- `BM-PERP-002` short open.
- `BM-PERP-003` long add.
- `BM-PERP-004` short add.
- `BM-PERP-005` partial close.
- `BM-PERP-006` full close.
- `BM-PERP-007` reduce-only check.
- `BM-PERP-008` fee charging.
- `BM-PERP-009` simplified funding settlement.
- `BM-PERP-014` liquidation detection.
- `BM-PERP-015` full liquidation.
- `BM-PERP-017` insurance fund debit.
- `BM-PERP-018` ADL ranking.
- `BM-PERP-019` simulate_fill accepted.
- `BM-PERP-020` simulate_fill rejected.
- `BM-PERP-025` mixed perp workload.

Future benchmark fields can include `trigger_evaluations`, snapshot restore counts, and event serialization counts only when those features exist.
