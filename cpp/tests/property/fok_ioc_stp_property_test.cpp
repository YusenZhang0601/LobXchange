#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/invariant_checker.hpp"
#include "test_helpers/test_framework.hpp"

#include <sstream>

using namespace lobx_test;

TEST(FokIocStpPropertyTest, FokStpNeverUsesSelfLiquidityAcrossExternalDepths) {
  constexpr uint64_t seed = 2026060303ULL;

  for (lob::Quantity external_qty = 1; external_qty <= 5; ++external_qty) {
    auto f = ExchangeFixture::Spot();
    const lob::Quantity requested_qty = external_qty + 1;

    std::ostringstream detail;
    detail << "seed=" << seed << " external_qty=" << external_qty << " requested_qty=" << requested_qty;

    EXPECT_TRUE_MSG(f.exchange.submit_limit(f.spot_symbol, f.alice, 19000 + external_qty * 10, lob::Side::Ask, 100, 5, lob::POST_ONLY, 1).accepted, detail.str());
    EXPECT_TRUE_MSG(f.exchange.submit_limit(f.spot_symbol, f.bob, 19001 + external_qty * 10, lob::Side::Ask, 100, external_qty, lob::POST_ONLY, 2).accepted, detail.str());

    const auto before_trades = f.exchange.trades().size();
    auto fok = f.exchange.submit_limit(f.spot_symbol, f.alice, 19002 + external_qty * 10, lob::Side::Bid, 100, requested_qty, lob::FOK | lob::STP, 3);
    EXPECT_TRUE_MSG(fok.accepted, detail.str() + " reason=" + fok.reason);
    EXPECT_EQ_MSG(fok.exec.filled, 0, detail.str());
    EXPECT_EQ_MSG(f.exchange.trades().size(), before_trades, detail.str());
    EXPECT_TRUE_MSG(f.exchange.topN(f.spot_symbol, lob::Side::Bid, 10).empty(), detail.str());
    require_invariants(f.exchange);
  }
}

TEST(FokIocStpPropertyTest, IocRemainderIsCanceledAfterPartialFill) {
  auto f = ExchangeFixture::Spot();

  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.bob, 19101, lob::Side::Ask, 100, 2, lob::POST_ONLY, 1).accepted);
  auto ioc = f.exchange.submit_limit(f.spot_symbol, f.alice, 19102, lob::Side::Bid, 100, 5, lob::IOC, 2);

  EXPECT_TRUE_MSG(ioc.accepted, "user=alice symbol=BTC-USDT order_id=19102 side=BUY price=100 qty=5 flags=IOC reason=" + ioc.reason);
  EXPECT_EQ(ioc.exec.filled, 2);
  EXPECT_EQ(ioc.exec.remaining, 3);
  EXPECT_TRUE(f.exchange.topN(f.spot_symbol, lob::Side::Bid, 10).empty());
  require_invariants(f.exchange);
}

TEST(FokIocStpPropertyTest, StpCancelLeavesAccountingConsistent) {
  SpotEngineFixture f;

  EXPECT_TRUE(f.submit(f.alice, 19201, lob::Side::Ask, 100, 3, lob::NONE, 1).accepted);
  auto stp = f.submit(f.alice, 19202, lob::Side::Bid, 100, 3, lob::IOC | lob::STP, 2);
  EXPECT_TRUE_MSG(stp.accepted, "user=alice symbol=BTC-USDT order_id=19202 side=BUY price=100 qty=3 flags=IOC|STP reason=" + stp.reason);
  EXPECT_EQ(stp.exec.filled, 0);

  auto report = check_order_book_invariants(f.engine, f.ledger);
  EXPECT_TRUE_MSG(report.ok, report_to_string(report));
  EXPECT_TRUE_MSG(f.ledger.invariant_ok(), f.ledger_summary(f.alice));
}
