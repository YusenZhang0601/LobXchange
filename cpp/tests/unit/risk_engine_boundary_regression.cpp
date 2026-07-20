#include "lobx/risk_engine.hpp"
#include "test_helpers/test_framework.hpp"

#include <limits>

namespace {

lobx::Market spot_market(int taker_fee_bps = 100) {
  return lobx::Market{1, "BTC-USDT", 1, 2, 2, lobx::MarketType::Spot, lobx::MarketStatus::Active,
                      1, 1, 1, 1, 0, taker_fee_bps, 1};
}

lobx::Market perp_market() {
  return lobx::Market{2, "BTC-USDT-PERP", 1, 2, 2, lobx::MarketType::Perpetual, lobx::MarketStatus::Active,
                      1, 1, 1, 1, 0, 0, 10};
}

} // namespace

TEST(RiskEngineBoundaryRegression, SpotBidLockAmountIncludesTakerFeeWorstCase) {
  lobx::AccountLedger ledger;
  lobx::RiskEngine risk;
  EXPECT_TRUE(ledger.deposit(10, 2, 101).ok);
  auto market = spot_market();
  lobx::OrderRequest order{market.id, 10, 1, 1, 1, lob::Side::Bid, 100, 1, lob::NONE};

  auto decision = risk.check_limit_order(order, market, ledger, nullptr, 1,
                                         std::numeric_limits<lob::Tick>::min(),
                                         std::numeric_limits<lob::Tick>::max(),
                                         false);

  EXPECT_TRUE(decision.accepted);
  EXPECT_EQ_MSG(decision.lock_amount, 101,
                "resting spot bid should either lock worst-case taker fee or document a different fee precheck rule");
}

TEST(RiskEngineBoundaryRegression, SpotBidExactNotionalWithoutFeeRejectedWhenCrossing) {
  lobx::AccountLedger ledger;
  lobx::RiskEngine risk;
  EXPECT_TRUE(ledger.deposit(10, 2, 100).ok);
  auto market = spot_market();
  lobx::OrderRequest order{market.id, 10, 1, 1, 1, lob::Side::Bid, 100, 1, lob::NONE};

  auto decision = risk.check_limit_order(order, market, ledger, nullptr, 1,
                                         std::numeric_limits<lob::Tick>::min(),
                                         100,
                                         false);

  EXPECT_FALSE(decision.accepted);
  EXPECT_EQ(decision.code, lobx::RejectCode::InsufficientBalance);
}

TEST(RiskEngineBoundaryRegression, SpotBidExactNotionalPlusFeeAcceptedWhenCrossing) {
  lobx::AccountLedger ledger;
  lobx::RiskEngine risk;
  EXPECT_TRUE(ledger.deposit(10, 2, 101).ok);
  auto market = spot_market();
  lobx::OrderRequest order{market.id, 10, 1, 1, 1, lob::Side::Bid, 100, 1, lob::NONE};

  auto decision = risk.check_limit_order(order, market, ledger, nullptr, 1,
                                         std::numeric_limits<lob::Tick>::min(),
                                         100,
                                         false);

  EXPECT_TRUE_MSG(decision.accepted, decision.reason);
  EXPECT_EQ(decision.lock_amount, 101);
}

TEST(RiskEngineBoundaryRegression, SpotTakerFeeOverflowRejected) {
  lobx::AccountLedger ledger;
  lobx::RiskEngine risk;
  EXPECT_TRUE(ledger.deposit(10, 2, std::numeric_limits<lobx::Amount>::max()).ok);
  auto market = spot_market(std::numeric_limits<int>::max());
  lobx::OrderRequest order{market.id, 10, 1, 1, 1, lob::Side::Bid, 100000000000LL, 1000000LL, lob::NONE};

  auto decision = risk.check_limit_order(order, market, ledger, nullptr, 1,
                                         std::numeric_limits<lob::Tick>::min(),
                                         100000000000LL,
                                         false);

  EXPECT_FALSE(decision.accepted);
  EXPECT_EQ(decision.code, lobx::RejectCode::InvalidNotional);
}

TEST(RiskEngineBoundaryRegression, PerpShortLimitBelowBestBidRequiresExecutionPriceMargin) {
  lobx::AccountLedger ledger;
  lobx::RiskEngine risk;
  lobx::PositionEngine positions;
  auto market = perp_market();
  EXPECT_TRUE(ledger.deposit(10, market.margin_asset, 20).ok);
  lobx::OrderRequest order{market.id, 10, 1, 1, 1, lob::Side::Ask, 90, 1, lob::NONE};

  auto decision = risk.check_limit_order(order, market, ledger, &positions, 5,
                                         100,
                                         std::numeric_limits<lob::Tick>::max(),
                                         false);

  EXPECT_TRUE_MSG(decision.accepted, decision.reason);
  EXPECT_EQ(decision.lock_amount, 20);
}

TEST(RiskEngineBoundaryRegression, PerpMarginOverflowRejected) {
  lobx::AccountLedger ledger;
  lobx::RiskEngine risk;
  lobx::PositionEngine positions;
  auto market = perp_market();
  EXPECT_TRUE(ledger.deposit(10, market.margin_asset, std::numeric_limits<lobx::Amount>::max()).ok);
  lobx::OrderRequest order{market.id, 10, 1, 1, 1, lob::Side::Bid,
                           std::numeric_limits<lob::Tick>::max(), 1, lob::NONE};

  auto decision = risk.check_limit_order(order, market, ledger, &positions, 10,
                                         std::numeric_limits<lob::Tick>::min(),
                                         std::numeric_limits<lob::Tick>::max(),
                                         false);

  EXPECT_FALSE(decision.accepted);
  EXPECT_EQ(decision.code, lobx::RejectCode::InvalidNotional);
}

TEST(RiskEngineBoundaryRegression, DuplicateOrderIdAfterFailedRiskCanRetryWithoutMutation) {
  lobx::AccountLedger ledger;
  lobx::RiskEngine risk;
  auto market = spot_market();
  EXPECT_TRUE(ledger.deposit(10, 2, 1000).ok);
  lobx::OrderRequest bad{market.id, 10, 1, 1, 1, lob::Side::Bid, 100, 1, 1u << 30};

  auto rejected = risk.check_limit_order(bad, market, ledger, nullptr, 1,
                                         std::numeric_limits<lob::Tick>::min(),
                                         std::numeric_limits<lob::Tick>::max(),
                                         false);
  lobx::OrderRequest good{market.id, 10, 1, 1, 2, lob::Side::Bid, 100, 1, lob::NONE};
  auto accepted = risk.check_limit_order(good, market, ledger, nullptr, 1,
                                         std::numeric_limits<lob::Tick>::min(),
                                         std::numeric_limits<lob::Tick>::max(),
                                         false);

  EXPECT_FALSE(rejected.accepted);
  EXPECT_TRUE_MSG(accepted.accepted, accepted.reason);
}

TEST(RiskEngineBoundaryRegression, FOKAndIOCCombinationHasDeterministicBehavior) {
  lobx::AccountLedger ledger;
  lobx::RiskEngine risk;
  auto market = spot_market();
  EXPECT_TRUE(ledger.deposit(10, 2, 1000).ok);
  lobx::OrderRequest order{market.id, 10, 1, 1, 1, lob::Side::Bid, 100, 1, lob::FOK | lob::IOC};

  auto decision = risk.check_limit_order(order, market, ledger, nullptr, 1,
                                         std::numeric_limits<lob::Tick>::min(),
                                         std::numeric_limits<lob::Tick>::max(),
                                         false);

  EXPECT_FALSE(decision.accepted);
  EXPECT_EQ(decision.code, lobx::RejectCode::UnsupportedOrderType);
}
