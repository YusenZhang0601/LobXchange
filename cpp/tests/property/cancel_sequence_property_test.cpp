#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/invariant_checker.hpp"
#include "test_helpers/test_framework.hpp"

#include <random>
#include <sstream>

using namespace lobx_test;

TEST(CancelSequencePropertyTest, FixedSeedPartialFillCancelSequencesDoNotDoubleRelease) {
  constexpr uint64_t seed = 2026060302ULL;
  std::mt19937_64 rng(seed);
  std::uniform_int_distribution<int> qty_dist(2, 8);

  for (int step = 0; step < 30; ++step) {
    auto f = ExchangeFixture::Spot();
    const lob::Quantity resting_qty = qty_dist(rng);
    const lob::Quantity fill_qty = resting_qty / 2;
    const lobx::OrderId ask_id = 18000 + step * 10;
    const lobx::OrderId bid_id = ask_id + 1;

    std::ostringstream detail;
    detail << "seed=" << seed << " step=" << step
           << " ask_id=" << ask_id << " resting_qty=" << resting_qty << " fill_qty=" << fill_qty;

    EXPECT_TRUE_MSG(f.exchange.submit_limit(f.spot_symbol, f.alice, ask_id, lob::Side::Ask, 100, resting_qty, lob::NONE, 1).accepted, detail.str());
    EXPECT_TRUE_MSG(f.exchange.submit_limit(f.spot_symbol, f.bob, bid_id, lob::Side::Bid, 100, fill_qty, lob::IOC, 2).accepted, detail.str());
    EXPECT_EQ_MSG(f.exchange.balance(f.alice, "BTC").locked, resting_qty - fill_qty, detail.str() + " " + f.wallet_summary(f.alice));

    EXPECT_TRUE_MSG(f.exchange.cancel(f.spot_symbol, ask_id), detail.str());
    EXPECT_EQ_MSG(f.exchange.balance(f.alice, "BTC").locked, 0, detail.str() + " " + f.wallet_summary(f.alice));
    EXPECT_FALSE_MSG(f.exchange.cancel(f.spot_symbol, ask_id), detail.str() + " repeated cancel must not double release");
    EXPECT_EQ_MSG(f.exchange.balance(f.alice, "BTC").locked, 0, detail.str() + " " + f.wallet_summary(f.alice));
    require_invariants(f.exchange);
  }
}

TEST(CancelSequencePropertyTest, CanceledOrdersDoNotContinueMatching) {
  auto f = ExchangeFixture::Spot();

  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.alice, 18101, lob::Side::Ask, 100, 5, lob::NONE, 1).accepted);
  EXPECT_TRUE(f.exchange.cancel(f.spot_symbol, 18101));
  auto bid = f.exchange.submit_limit(f.spot_symbol, f.bob, 18102, lob::Side::Bid, 100, 5, lob::IOC, 2);

  EXPECT_TRUE(bid.accepted);
  EXPECT_EQ(bid.exec.filled, 0);
  EXPECT_TRUE(f.exchange.trades().empty());
  require_invariants(f.exchange);
}
