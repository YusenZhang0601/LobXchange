#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/invariant_checker.hpp"
#include "test_helpers/test_framework.hpp"

#include <random>
#include <sstream>

using namespace lobx_test;

TEST(MarketEngineInvariantsProperty, LedgerAndBookInvariantsHoldAfterSubmitAndCancel) {
  constexpr uint64_t seed = 2026060306ULL;
  std::mt19937_64 rng(seed);
  std::uniform_int_distribution<int> side_dist(0, 1);
  std::uniform_int_distribution<int> price_dist(95, 105);
  std::uniform_int_distribution<int> qty_dist(1, 4);
  std::uniform_int_distribution<int> flag_dist(0, 3);

  SpotEngineFixture f;

  for (int step = 0; step < 80; ++step) {
    const lobx::UserId user = side_dist(rng) == 0 ? f.alice : f.bob;
    const lob::Side side = side_dist(rng) == 0 ? lob::Side::Bid : lob::Side::Ask;
    const lob::Tick price = price_dist(rng);
    const lob::Quantity qty = qty_dist(rng);
    const uint32_t flags = flag_dist(rng) == 0 ? lob::IOC : (flag_dist(rng) == 1 ? lob::FOK : lob::NONE);
    const lobx::OrderId order_id = 37000 + step;

    auto result = f.submit(user, order_id, side, price, qty, flags, step + 1);
    std::ostringstream detail;
    detail << "seed=" << seed << " step=" << step
           << " user=" << user << " order_id=" << order_id
           << " side=" << (side == lob::Side::Bid ? "BUY" : "SELL")
           << " price=" << price << " qty=" << qty << " flags=" << flags
           << " accepted=" << result.accepted << " reason=" << result.reason;
    EXPECT_NE_MSG(result.code, lobx::RejectCode::InternalError, detail.str());
    EXPECT_TRUE_MSG(f.ledger.invariant_ok(), detail.str());

    auto report = check_order_book_invariants(f.engine, f.ledger);
    EXPECT_TRUE_MSG(report.ok, detail.str() + "\n" + report_to_string(report));
  }
}

TEST(MarketEngineInvariantsProperty, IocAndFokOrdersNeverRemainOpen) {
  SpotEngineFixture f;

  for (int i = 0; i < 20; ++i) {
    const uint32_t flags = i % 2 == 0 ? lob::IOC : lob::FOK;
    auto result = f.submit(f.alice, 38000 + i, lob::Side::Bid, 100 + i, 1, flags, i + 1);
    EXPECT_TRUE_MSG(result.accepted, "order_id=" + std::to_string(38000 + i) + " flags=" + std::to_string(flags) + " reason=" + result.reason);

    auto report = check_order_book_invariants(f.engine, f.ledger);
    EXPECT_TRUE_MSG(report.ok, report_to_string(report));
  }
}

TEST(MarketEngineInvariantsProperty, PerpReduceOnlyNeverIncreasesAbsExposure) {
  PerpEngineFixture f;

  EXPECT_TRUE(f.submit(f.bob, 39001, lob::Side::Ask, 100, 5, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.alice, 39002, lob::Side::Bid, 100, 5, lob::IOC, 2).accepted);
  const auto before = std::llabs(f.positions.position(f.alice, f.market.id).signed_qty);

  auto rejected = f.submit(f.alice, 39003, lob::Side::Bid, 100, 1, lobx::LOBX_REDUCE_ONLY | lob::IOC, 3);
  EXPECT_FALSE(rejected.accepted);
  EXPECT_EQ(rejected.code, lobx::RejectCode::ReduceOnlyWouldIncrease);
  const auto after = std::llabs(f.positions.position(f.alice, f.market.id).signed_qty);
  EXPECT_TRUE(after <= before);
}
