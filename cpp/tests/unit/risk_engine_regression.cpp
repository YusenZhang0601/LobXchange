#include "lobx/risk_engine.hpp"
#include "test_helpers/test_framework.hpp"

#include <limits>

namespace {

lobx::Market spot_market(int taker_fee_bps = 0) {
  return lobx::Market{1,
                      "BTC-USDT",
                      1,
                      2,
                      2,
                      lobx::MarketType::Spot,
                      lobx::MarketStatus::Active,
                      1,
                      1,
                      1,
                      1,
                      0,
                      taker_fee_bps,
                      1};
}

lobx::Market perp_market() {
  return lobx::Market{2,
                      "BTC-USDT-PERP",
                      1,
                      2,
                      2,
                      lobx::MarketType::Perpetual,
                      lobx::MarketStatus::Active,
                      1,
                      1,
                      1,
                      1,
                      0,
                      0,
                      10};
}

lobx::OrderRequest req(lob::Side side, lob::Tick price, lob::Quantity qty, uint32_t flags = lob::NONE) {
  return lobx::OrderRequest{1, 10, 1, 1, 1, side, price, qty, flags};
}

} // namespace

TEST(RiskEngineRegression, SpotBidLocksQuoteWorstCaseNotional) {
  lobx::AccountLedger ledger;
  lobx::RiskEngine risk;
  EXPECT_TRUE(ledger.deposit(10, 2, 1000).ok);

  auto decision = risk.check_limit_order(req(lob::Side::Bid, 100, 3), spot_market(), ledger, nullptr, 1,
                                         std::numeric_limits<lob::Tick>::min(),
                                         std::numeric_limits<lob::Tick>::max(),
                                         false);

  EXPECT_TRUE_MSG(decision.accepted, decision.reason);
  EXPECT_EQ(decision.lock_asset, 2U);
  EXPECT_EQ(decision.lock_amount, 300);
}

TEST(RiskEngineRegression, SpotAskLocksBaseQuantity) {
  lobx::AccountLedger ledger;
  lobx::RiskEngine risk;
  EXPECT_TRUE(ledger.deposit(10, 1, 10).ok);

  auto decision = risk.check_limit_order(req(lob::Side::Ask, 100, 3), spot_market(), ledger, nullptr, 1,
                                         std::numeric_limits<lob::Tick>::min(),
                                         std::numeric_limits<lob::Tick>::max(),
                                         false);

  EXPECT_TRUE_MSG(decision.accepted, decision.reason);
  EXPECT_EQ(decision.lock_asset, 1U);
  EXPECT_EQ(decision.lock_amount, 3);
}

TEST(RiskEngineRegression, DuplicateOrderIdRejectedBeforeLock) {
  lobx::AccountLedger ledger;
  lobx::RiskEngine risk;
  EXPECT_TRUE(ledger.deposit(10, 2, 1000).ok);

  auto decision = risk.check_limit_order(req(lob::Side::Bid, 100, 1), spot_market(), ledger, nullptr, 1,
                                         std::numeric_limits<lob::Tick>::min(),
                                         std::numeric_limits<lob::Tick>::max(),
                                         true);

  EXPECT_FALSE(decision.accepted);
  EXPECT_EQ(decision.code, lobx::RejectCode::DuplicateOrderId);
  EXPECT_EQ(ledger.locked(10, 2), 0);
}

TEST(RiskEngineRegression, PerpReduceOnlyAllowedWhenReducing) {
  lobx::AccountLedger ledger;
  lobx::RiskEngine risk;
  lobx::PositionEngine positions;
  auto market = perp_market();
  positions.apply_trade(10, market.id, lob::Side::Bid, 100, 3);

  lobx::OrderRequest order{market.id, 10, 1, 1, 1, lob::Side::Ask, 100, 2, lobx::LOBX_REDUCE_ONLY};
  auto decision = risk.check_limit_order(order, market, ledger, &positions, 5,
                                         std::numeric_limits<lob::Tick>::min(),
                                         std::numeric_limits<lob::Tick>::max(),
                                         false);

  EXPECT_TRUE_MSG(decision.accepted, decision.reason);
  EXPECT_EQ(decision.lock_amount, 0);
}

TEST(RiskEngineRegression, PerpReduceOnlyRejectedWhenIncreasing) {
  lobx::AccountLedger ledger;
  lobx::RiskEngine risk;
  lobx::PositionEngine positions;
  auto market = perp_market();
  positions.apply_trade(10, market.id, lob::Side::Bid, 100, 3);

  lobx::OrderRequest order{market.id, 10, 1, 1, 1, lob::Side::Bid, 100, 1, lobx::LOBX_REDUCE_ONLY};
  auto decision = risk.check_limit_order(order, market, ledger, &positions, 5,
                                         std::numeric_limits<lob::Tick>::min(),
                                         std::numeric_limits<lob::Tick>::max(),
                                         false);

  EXPECT_FALSE(decision.accepted);
  EXPECT_EQ(decision.code, lobx::RejectCode::ReduceOnlyWouldIncrease);
}
