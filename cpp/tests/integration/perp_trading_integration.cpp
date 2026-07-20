#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/invariant_checker.hpp"
#include "test_helpers/test_framework.hpp"

using namespace lobx_test;

TEST(PerpTradingIntegration, OpensLongAndShortFromOneTrade) {
  auto f = ExchangeFixture::Perp();

  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.bob, 13001, lob::Side::Ask, 100, 4, lob::POST_ONLY, 1).accepted);
  auto buy = f.exchange.submit_limit(f.perp_symbol, f.alice, 13002, lob::Side::Bid, 100, 4, lob::IOC, 2);

  EXPECT_TRUE_MSG(buy.accepted, "user=alice symbol=BTC-USDT-PERP order_id=13002 side=BUY price=100 qty=4 flags=IOC reason=" + buy.reason);
  EXPECT_EQ(f.exchange.position(f.alice, f.perp_symbol).signed_qty, 4);
  EXPECT_EQ(f.exchange.position(f.alice, f.perp_symbol).entry_price, 100);
  EXPECT_EQ(f.exchange.position(f.bob, f.perp_symbol).signed_qty, -4);
  EXPECT_EQ(f.exchange.position(f.bob, f.perp_symbol).entry_price, 100);
  require_invariants(f.exchange);
}

TEST(PerpTradingIntegration, IncreasePositionUpdatesAverageEntry) {
  auto f = ExchangeFixture::Perp();

  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.bob, 13011, lob::Side::Ask, 100, 5, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.alice, 13012, lob::Side::Bid, 100, 5, lob::IOC, 2).accepted);
  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.carol, 13013, lob::Side::Ask, 120, 5, lob::POST_ONLY, 3).accepted);
  auto add = f.exchange.submit_limit(f.perp_symbol, f.alice, 13014, lob::Side::Bid, 120, 5, lob::IOC, 4);

  EXPECT_TRUE_MSG(add.accepted, "user=alice symbol=BTC-USDT-PERP order_id=13014 side=BUY price=120 qty=5 flags=IOC reason=" + add.reason);
  EXPECT_EQ(f.exchange.position(f.alice, f.perp_symbol).signed_qty, 10);
  EXPECT_EQ(f.exchange.position(f.alice, f.perp_symbol).entry_price, 110);
  require_invariants(f.exchange);
}

TEST(PerpTradingIntegration, PartialAndFullCloseSettlePnlAndReleaseMargin) {
  auto f = ExchangeFixture::Perp();
  const auto initial = f.exchange.balance(f.alice, "USDT").total;

  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.bob, 13021, lob::Side::Ask, 100, 4, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.alice, 13022, lob::Side::Bid, 100, 4, lob::IOC, 2).accepted);

  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.carol, 13023, lob::Side::Bid, 110, 2, lob::POST_ONLY, 3).accepted);
  auto partial = f.exchange.submit_limit(f.perp_symbol, f.alice, 13024, lob::Side::Ask, 110, 2, lobx::LOBX_REDUCE_ONLY | lob::IOC, 4);
  EXPECT_TRUE_MSG(partial.accepted, "partial close reason=" + partial.reason);
  EXPECT_EQ(f.exchange.position(f.alice, f.perp_symbol).signed_qty, 2);
  EXPECT_EQ(f.exchange.position(f.alice, f.perp_symbol).realized_pnl, 20);

  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.carol, 13025, lob::Side::Bid, 120, 2, lob::POST_ONLY, 5).accepted);
  auto full = f.exchange.submit_limit(f.perp_symbol, f.alice, 13026, lob::Side::Ask, 120, 2, lobx::LOBX_REDUCE_ONLY | lob::IOC, 6);
  EXPECT_TRUE_MSG(full.accepted, "full close reason=" + full.reason);
  EXPECT_EQ(f.exchange.position(f.alice, f.perp_symbol).signed_qty, 0);
  EXPECT_EQ(f.exchange.position(f.alice, f.perp_symbol).realized_pnl, 60);
  EXPECT_EQ_MSG(f.exchange.balance(f.alice, "USDT").total, initial + 60, f.wallet_summary(f.alice));
  EXPECT_EQ_MSG(f.exchange.balance(f.alice, "USDT").locked, 0, f.wallet_summary(f.alice));
  require_invariants(f.exchange);
}

TEST(PerpTradingIntegration, ReduceOnlyCannotIncreaseOrFlipPosition) {
  auto f = ExchangeFixture::Perp();

  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.bob, 13031, lob::Side::Ask, 100, 4, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.alice, 13032, lob::Side::Bid, 100, 4, lob::IOC, 2).accepted);

  auto increase = f.exchange.submit_limit(f.perp_symbol, f.alice, 13033, lob::Side::Bid, 100, 1, lobx::LOBX_REDUCE_ONLY | lob::IOC, 3);
  EXPECT_FALSE(increase.accepted);
  EXPECT_EQ(increase.code, lobx::RejectCode::ReduceOnlyWouldIncrease);

  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.carol, 13034, lob::Side::Bid, 100, 6, lob::POST_ONLY, 4).accepted);
  auto flip = f.exchange.submit_limit(f.perp_symbol, f.alice, 13035, lob::Side::Ask, 100, 6, lobx::LOBX_REDUCE_ONLY | lob::IOC, 5);
  EXPECT_FALSE(flip.accepted);
  EXPECT_EQ(flip.code, lobx::RejectCode::ReduceOnlyWouldIncrease);
  EXPECT_EQ(f.exchange.position(f.alice, f.perp_symbol).signed_qty, 4);
  require_invariants(f.exchange);
}
