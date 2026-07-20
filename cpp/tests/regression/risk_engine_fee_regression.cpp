#include "lobx/risk_engine.hpp"
#include "test_helpers/test_framework.hpp"

#include <limits>

TEST(RiskEngineFeeRegression, SpotBidLockIncludesEstimatedTakerFee) {
  lobx::AccountLedger ledger;
  lobx::RiskEngine risk;
  lobx::Market market{1,
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
                      100,
                      1};
  EXPECT_TRUE(ledger.deposit(10, 2, 101).ok);

  lobx::OrderRequest order{market.id, 10, 1, 1, 1, lob::Side::Bid, 100, 1, lob::NONE};
  auto decision = risk.check_limit_order(order, market, ledger, nullptr, 1,
                                         std::numeric_limits<lob::Tick>::min(),
                                         100,
                                         false);

  EXPECT_TRUE_MSG(decision.accepted, "setup should accept when notional plus fee is available reason=" + decision.reason);
  EXPECT_EQ_MSG(decision.lock_amount, 101,
                "spot bid risk lock should include estimated taker fee when the order immediately crosses");
}
