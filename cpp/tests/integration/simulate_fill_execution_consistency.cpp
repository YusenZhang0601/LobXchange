#include "test_helpers/market_microstructure_helpers.hpp"

#include <cmath>
#include <limits>
#include <vector>

using namespace lobx_test;

namespace {

constexpr lobx::UserId dave = 40;

struct OpenOrderView {
  lobx::OrderId id{0};
  lobx::UserId user{0};
  lob::SeqNo seq{0};
  lob::Side side{lob::Side::Bid};
  lob::Tick limit_price{0};
  lob::Quantity leaves_qty{0};
  lob::Timestamp ts{0};
  lobx::AssetId locked_asset{0};
  lobx::Amount locked_remaining{0};
  uint32_t flags{lob::NONE};

  bool operator==(const OpenOrderView&) const = default;
};

std::vector<OpenOrderView> open_order_views(const SpotEngineFixture& f) {
  std::vector<OpenOrderView> out;
  for (const auto& order : f.engine.open_orders()) {
    out.push_back(OpenOrderView{order.id,
                                order.user,
                                order.seq,
                                order.side,
                                order.limit_price,
                                order.leaves_qty,
                                order.ts,
                                order.locked_asset,
                                order.locked_remaining,
                                order.flags});
  }
  return out;
}

void deposit_dave(SpotEngineFixture& f) {
  deposit_spot_user(f, dave);
}

void expect_avg_price(const lobx::SimulatedFill& sim, long double expected) {
  EXPECT_TRUE_MSG(std::fabs(sim.avg_price - expected) < 0.000001L,
                  "avg_price=" + std::to_string(static_cast<double>(sim.avg_price)));
}

} // namespace

TEST(SimulateFillExecutionConsistency, SimulateFillDoesNotMutateBookOpenLedgerOrEvents) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 58001, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  const auto bids_before = f.engine.topN(lob::Side::Bid, 100);
  const auto asks_before = f.engine.topN(lob::Side::Ask, 100);
  const auto open_before = open_order_views(f);
  const auto alice_base_before = f.ledger.balance(f.alice, f.base_asset);
  const auto alice_quote_before = f.ledger.balance(f.alice, f.quote_asset);
  const auto bob_base_before = f.ledger.balance(f.bob, f.base_asset);
  const auto bob_quote_before = f.ledger.balance(f.bob, f.quote_asset);
  const auto events_before = f.events.records().size();

  auto sim = f.engine.simulate_fill(f.bob, lob::Side::Bid, 100, 1, lob::IOC);

  EXPECT_EQ(sim.fillable_qty, 1);
  EXPECT_TRUE(f.engine.topN(lob::Side::Bid, 100) == bids_before);
  EXPECT_TRUE(f.engine.topN(lob::Side::Ask, 100) == asks_before);
  EXPECT_TRUE(open_order_views(f) == open_before);
  EXPECT_EQ(f.ledger.balance(f.alice, f.base_asset).total, alice_base_before.total);
  EXPECT_EQ(f.ledger.balance(f.alice, f.base_asset).locked, alice_base_before.locked);
  EXPECT_EQ(f.ledger.balance(f.alice, f.quote_asset).total, alice_quote_before.total);
  EXPECT_EQ(f.ledger.balance(f.bob, f.base_asset).total, bob_base_before.total);
  EXPECT_EQ(f.ledger.balance(f.bob, f.quote_asset).total, bob_quote_before.total);
  EXPECT_EQ(f.ledger.balance(f.bob, f.quote_asset).locked, bob_quote_before.locked);
  EXPECT_EQ(f.events.records().size(), events_before);
}

TEST(SimulateFillExecutionConsistency, SimulateFillSingleLevelMatchesExecution) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 58011, lob::Side::Ask, 100, 3, lob::POST_ONLY, 1).accepted);

  auto sim = f.engine.simulate_fill(f.bob, lob::Side::Bid, 100, 2, lob::IOC);
  auto bid = f.submit(f.bob, 58012, lob::Side::Bid, 100, 2, lob::IOC, 2);

  EXPECT_EQ(sim.fillable_qty, 2);
  EXPECT_EQ(sim.notional, 200);
  EXPECT_EQ(sim.worst_price, 100);
  expect_avg_price(sim, 100.0L);
  EXPECT_EQ(sim.levels_consumed, 1);
  EXPECT_TRUE(sim.fok_would_fill);
  EXPECT_TRUE_MSG(bid.accepted, bid.reason);
  EXPECT_EQ(bid.trades.size(), 1UL);
  expect_trade(bid.trades.front(), f.bob, f.alice, 58012, 58011, 100, 2);
}

TEST(SimulateFillExecutionConsistency, SimulateFillMultiLevelMatchesExecution) {
  SpotEngineFixture f;
  deposit_dave(f);
  EXPECT_TRUE(f.submit(f.alice, 58021, lob::Side::Ask, 90, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.carol, 58022, lob::Side::Ask, 95, 2, lob::POST_ONLY, 2).accepted);
  EXPECT_TRUE(f.submit(dave, 58023, lob::Side::Ask, 100, 3, lob::POST_ONLY, 3).accepted);

  auto sim = f.engine.simulate_fill(f.bob, lob::Side::Bid, 100, 4, lob::IOC);
  auto bid = f.submit(f.bob, 58024, lob::Side::Bid, 100, 4, lob::IOC, 4);

  EXPECT_EQ(sim.fillable_qty, 4);
  EXPECT_EQ(sim.notional, 380);
  EXPECT_EQ(sim.worst_price, 100);
  expect_avg_price(sim, 95.0L);
  EXPECT_EQ(sim.levels_consumed, 3);
  EXPECT_TRUE_MSG(bid.accepted, bid.reason);
  EXPECT_EQ(bid.trades.size(), 3UL);
  expect_trade(bid.trades[0], f.bob, f.alice, 58024, 58021, 90, 1);
  expect_trade(bid.trades[1], f.bob, f.carol, 58024, 58022, 95, 2);
  expect_trade(bid.trades[2], f.bob, dave, 58024, 58023, 100, 1);
}

TEST(SimulateFillExecutionConsistency, SimulateFillRespectsLimitPrice) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 58031, lob::Side::Ask, 90, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.carol, 58032, lob::Side::Ask, 101, 1, lob::POST_ONLY, 2).accepted);

  auto sim = f.engine.simulate_fill(f.bob, lob::Side::Bid, 100, 2, lob::IOC);
  auto bid = f.submit(f.bob, 58033, lob::Side::Bid, 100, 2, lob::IOC, 3);

  EXPECT_EQ(sim.fillable_qty, 1);
  EXPECT_EQ(sim.notional, 90);
  EXPECT_EQ(sim.worst_price, 90);
  EXPECT_EQ(sim.levels_consumed, 1);
  EXPECT_TRUE_MSG(bid.accepted, bid.reason);
  EXPECT_EQ(bid.trades.size(), 1UL);
  EXPECT_EQ(bid.trades.front().price, 90);
  EXPECT_TRUE(has_open_order(f, 58032));
}

TEST(SimulateFillExecutionConsistency, SimulateFillAskSideMultiLevelMatchesExecution) {
  SpotEngineFixture f;
  deposit_dave(f);
  EXPECT_TRUE(f.submit(f.alice, 58041, lob::Side::Bid, 110, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.carol, 58042, lob::Side::Bid, 105, 2, lob::POST_ONLY, 2).accepted);
  EXPECT_TRUE(f.submit(dave, 58043, lob::Side::Bid, 100, 3, lob::POST_ONLY, 3).accepted);

  auto sim = f.engine.simulate_fill(f.bob, lob::Side::Ask, 100, 4, lob::IOC);
  auto ask = f.submit(f.bob, 58044, lob::Side::Ask, 100, 4, lob::IOC, 4);

  EXPECT_EQ(sim.fillable_qty, 4);
  EXPECT_EQ(sim.notional, 420);
  EXPECT_EQ(sim.worst_price, 100);
  expect_avg_price(sim, 105.0L);
  EXPECT_EQ(sim.levels_consumed, 3);
  EXPECT_TRUE_MSG(ask.accepted, ask.reason);
  EXPECT_EQ(ask.trades.size(), 3UL);
  expect_trade(ask.trades[0], f.alice, f.bob, 58041, 58044, 110, 1);
  expect_trade(ask.trades[1], f.carol, f.bob, 58042, 58044, 105, 2);
  expect_trade(ask.trades[2], dave, f.bob, 58043, 58044, 100, 1);
}

TEST(SimulateFillExecutionConsistency, SimulateFillRespectsSTP) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.bob, 58051, lob::Side::Ask, 90, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.alice, 58052, lob::Side::Ask, 95, 1, lob::POST_ONLY, 2).accepted);

  auto sim = f.engine.simulate_fill(f.bob, lob::Side::Bid, 100, 2, lob::IOC | lob::STP);
  auto bid = f.submit(f.bob, 58053, lob::Side::Bid, 100, 2, lob::IOC | lob::STP, 3);

  EXPECT_EQ(sim.self_liquidity_skipped, 1);
  EXPECT_EQ(sim.fillable_qty, 1);
  EXPECT_EQ(sim.notional, 95);
  EXPECT_EQ(sim.worst_price, 95);
  EXPECT_EQ(sim.levels_consumed, 1);
  EXPECT_TRUE_MSG(bid.accepted, bid.reason);
  EXPECT_EQ(bid.trades.size(), 1UL);
  expect_trade(bid.trades.front(), f.bob, f.alice, 58053, 58052, 95, 1);
}

TEST(SimulateFillExecutionConsistency, SimulateFillFOKAvailabilityMatchesExecution) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 58061, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);

  auto sim = f.engine.simulate_fill(f.bob, lob::Side::Bid, 100, 2, lob::FOK);
  auto bid = f.submit(f.bob, 58062, lob::Side::Bid, 100, 2, lob::FOK, 2);

  EXPECT_EQ(sim.fillable_qty, 1);
  EXPECT_FALSE(sim.fok_would_fill);
  EXPECT_TRUE_MSG(bid.accepted, bid.reason);
  EXPECT_TRUE(bid.trades.empty());
  EXPECT_TRUE(has_open_order(f, 58061));
}

TEST(SimulateFillExecutionConsistency, SimulateFillFOKFullLiquidityMatchesExecution) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 58071, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.carol, 58072, lob::Side::Ask, 100, 1, lob::POST_ONLY, 2).accepted);

  auto sim = f.engine.simulate_fill(f.bob, lob::Side::Bid, 100, 2, lob::FOK);
  auto bid = f.submit(f.bob, 58073, lob::Side::Bid, 100, 2, lob::FOK, 3);

  EXPECT_EQ(sim.fillable_qty, 2);
  EXPECT_TRUE(sim.fok_would_fill);
  EXPECT_TRUE_MSG(bid.accepted, bid.reason);
  EXPECT_EQ(bid.trades.size(), 2UL);
}

TEST(SimulateFillExecutionConsistency, SimulateFillFeeEstimateMatchesActualFee) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);
  EXPECT_TRUE(f.submit(f.alice, 58081, lob::Side::Ask, 10000, 2, lob::POST_ONLY, 1).accepted);
  const auto fee_before = f.ledger.balance(dedicated_fee_account(), f.quote_asset).total;

  auto sim = f.engine.simulate_fill(f.bob, lob::Side::Bid, 10000, 2, lob::IOC);
  auto bid = f.submit(f.bob, 58082, lob::Side::Bid, 10000, 2, lob::IOC, 2);

  EXPECT_EQ(sim.notional, 20000);
  EXPECT_EQ(sim.estimated_taker_fee, 200);
  EXPECT_TRUE_MSG(bid.accepted, bid.reason);
  EXPECT_EQ(f.ledger.balance(dedicated_fee_account(), f.quote_asset).total - fee_before, sim.estimated_taker_fee);
}

TEST(SimulateFillExecutionConsistency, SimulateFillRequiredLockMatchesRiskDecision) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);
  lobx::OrderRequest req{f.market.id, f.bob, 58091, f.events.next_seq(), 1, lob::Side::Bid, 10000, 2, lob::IOC};

  auto sim = f.engine.simulate_fill(req);
  const auto decision = f.risk.check_limit_order(req, f.market, f.ledger, &f.positions, 1,
                                                 f.engine.best_bid(), f.engine.best_ask(), false);

  EXPECT_TRUE_MSG(decision.accepted, decision.reason);
  EXPECT_EQ(sim.lock_asset, decision.lock_asset);
  EXPECT_EQ(sim.required_lock, decision.lock_amount);
  EXPECT_EQ(sim.lock_asset, f.quote_asset);
  EXPECT_EQ(sim.required_lock, 20200);
}

TEST(SimulateFillExecutionConsistency, SimulateFillPostOnlyCrossingReportsReject) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 58101, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);

  auto sim = f.engine.simulate_fill(f.bob, lob::Side::Bid, 100, 1, lob::POST_ONLY);
  auto bid = f.submit(f.bob, 58102, lob::Side::Bid, 100, 1, lob::POST_ONLY, 2);

  EXPECT_EQ(sim.code, lobx::RejectCode::PostOnlyWouldCross);
  EXPECT_EQ(sim.fillable_qty, 0);
  EXPECT_FALSE(sim.would_rest);
  EXPECT_FALSE(bid.accepted);
}

TEST(SimulateFillExecutionConsistency, SimulateFillPostOnlyNonCrossingWouldRest) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 58111, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);

  auto sim = f.engine.simulate_fill(f.bob, lob::Side::Bid, 99, 1, lob::POST_ONLY);
  auto bid = f.submit(f.bob, 58112, lob::Side::Bid, 99, 1, lob::POST_ONLY, 2);

  EXPECT_EQ(sim.code, lobx::RejectCode::None);
  EXPECT_EQ(sim.fillable_qty, 0);
  EXPECT_TRUE(sim.would_rest);
  EXPECT_TRUE_MSG(bid.accepted, bid.reason);
  EXPECT_TRUE(has_open_order(f, 58112));
}
