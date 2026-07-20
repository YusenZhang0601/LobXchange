#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/invariant_checker.hpp"
#include "test_helpers/test_framework.hpp"

#include <random>
#include <sstream>

using namespace lobx_test;

TEST(PerpPositionPropertyTest, FixedSeedLongIncreaseAndCloseSequencesKeepWalletMarginAndPnlConsistent) {
  constexpr uint64_t seed = 2026060304ULL;
  std::mt19937_64 rng(seed);
  std::uniform_int_distribution<int> qty_dist(1, 4);
  std::uniform_int_distribution<int> price_dist(90, 110);

  for (int step = 0; step < 12; ++step) {
    auto f = ExchangeFixture::Perp();
    const lob::Quantity qty = qty_dist(rng);
    const lob::Tick entry = price_dist(rng);
    const lob::Tick add_price = entry + 10;
    const lob::Tick close_one = entry + 5;
    const lob::Tick close_two = entry + 15;
    const lobx::OrderId base = 20000 + step * 20;
    const auto initial = f.exchange.balance(f.alice, "USDT").total;

    std::ostringstream detail;
    detail << "seed=" << seed << " step=" << step << " qty=" << qty << " entry=" << entry;

    EXPECT_TRUE_MSG(f.exchange.submit_limit(f.perp_symbol, f.bob, base + 1, lob::Side::Ask, entry, qty, lob::POST_ONLY, 1).accepted, detail.str());
    EXPECT_TRUE_MSG(f.exchange.submit_limit(f.perp_symbol, f.alice, base + 2, lob::Side::Bid, entry, qty, lob::IOC, 2).accepted, detail.str());
    require_invariants(f.exchange);

    EXPECT_TRUE_MSG(f.exchange.submit_limit(f.perp_symbol, f.carol, base + 3, lob::Side::Ask, add_price, qty, lob::POST_ONLY, 3).accepted, detail.str());
    EXPECT_TRUE_MSG(f.exchange.submit_limit(f.perp_symbol, f.alice, base + 4, lob::Side::Bid, add_price, qty, lob::IOC, 4).accepted, detail.str());
    EXPECT_EQ_MSG(f.exchange.position(f.alice, f.perp_symbol).signed_qty, qty * 2, detail.str());
    EXPECT_EQ_MSG(f.exchange.position(f.alice, f.perp_symbol).entry_price, entry + 5, detail.str());
    require_invariants(f.exchange);

    EXPECT_TRUE_MSG(f.exchange.submit_limit(f.perp_symbol, f.bob, base + 5, lob::Side::Bid, close_one, qty, lob::POST_ONLY, 5).accepted, detail.str());
    EXPECT_TRUE_MSG(f.exchange.submit_limit(f.perp_symbol, f.alice, base + 6, lob::Side::Ask, close_one, qty, lobx::LOBX_REDUCE_ONLY | lob::IOC, 6).accepted, detail.str());
    EXPECT_EQ_MSG(f.exchange.position(f.alice, f.perp_symbol).signed_qty, qty, detail.str());
    require_invariants(f.exchange);

    EXPECT_TRUE_MSG(f.exchange.submit_limit(f.perp_symbol, f.carol, base + 7, lob::Side::Bid, close_two, qty, lob::POST_ONLY, 7).accepted, detail.str());
    EXPECT_TRUE_MSG(f.exchange.submit_limit(f.perp_symbol, f.alice, base + 8, lob::Side::Ask, close_two, qty, lobx::LOBX_REDUCE_ONLY | lob::IOC, 8).accepted, detail.str());
    const auto position = f.exchange.position(f.alice, f.perp_symbol);
    EXPECT_EQ_MSG(position.signed_qty, 0, detail.str());
    EXPECT_EQ_MSG(f.exchange.balance(f.alice, "USDT").total, initial + position.realized_pnl, detail.str() + " " + f.wallet_summary(f.alice));
    EXPECT_EQ_MSG(f.exchange.balance(f.alice, "USDT").locked, 0, detail.str() + " " + f.wallet_summary(f.alice));
    require_invariants(f.exchange);
  }
}

TEST(PerpPositionPropertyTest, ReduceOnlyRejectsRandomRiskIncreasingDirections) {
  constexpr uint64_t seed = 2026060305ULL;
  std::mt19937_64 rng(seed);
  std::uniform_int_distribution<int> qty_dist(1, 5);

  for (int step = 0; step < 12; ++step) {
    auto f = ExchangeFixture::Perp();
    const lob::Quantity qty = qty_dist(rng);
    const lobx::OrderId base = 21000 + step * 10;

    EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.bob, base + 1, lob::Side::Ask, 100, qty, lob::POST_ONLY, 1).accepted);
    EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.alice, base + 2, lob::Side::Bid, 100, qty, lob::IOC, 2).accepted);

    auto increase = f.exchange.submit_limit(f.perp_symbol, f.alice, base + 3, lob::Side::Bid, 100, 1, lobx::LOBX_REDUCE_ONLY | lob::IOC, 3);
    EXPECT_FALSE_MSG(increase.accepted, "seed=" + std::to_string(seed) + " step=" + std::to_string(step));
    EXPECT_EQ(increase.code, lobx::RejectCode::ReduceOnlyWouldIncrease);
    require_invariants(f.exchange);
  }
}
