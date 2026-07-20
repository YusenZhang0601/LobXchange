#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/invariant_checker.hpp"
#include "test_helpers/test_framework.hpp"

using namespace lobx_test;

TEST(ExchangePerpConsistency, PerpOpenLongShortCreatesOneCommittedTrade) {
  auto f = ExchangeFixture::Perp();

  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.bob, 36001, lob::Side::Ask, 100, 3, lob::POST_ONLY, 1).accepted);
  auto buy = f.exchange.submit_limit(f.perp_symbol, f.alice, 36002, lob::Side::Bid, 100, 3, lob::IOC, 2);

  EXPECT_TRUE_MSG(buy.accepted, "user=alice symbol=BTC-USDT-PERP order_id=36002 side=BUY price=100 qty=3 flags=IOC reason=" + buy.reason);
  EXPECT_EQ(f.exchange.trades().size(), 1UL);
  EXPECT_EQ(f.exchange.position(f.alice, f.perp_symbol).signed_qty, 3);
  EXPECT_EQ(f.exchange.position(f.bob, f.perp_symbol).signed_qty, -3);
  require_invariants(f.exchange);
}

TEST(ExchangePerpConsistency, PerpRejectLossCloseIfMarginInsufficientDoesNotChangeTradeHistory) {
  auto f = ExchangeFixture::Perp();
  f.exchange.ledger().withdraw(f.alice, 1, f.exchange.balance(f.alice, "USDT").free - 1000);

  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.bob, 36011, lob::Side::Ask, 100, 50, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.alice, 36012, lob::Side::Bid, 100, 50, lob::IOC, 2).accepted);
  const auto trades_before = f.exchange.trades().size();
  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.carol, 36013, lob::Side::Bid, 1, 50, lob::POST_ONLY, 3).accepted);

  auto close = f.exchange.submit_limit(f.perp_symbol, f.alice, 36014, lob::Side::Ask, 1, 50, lobx::LOBX_REDUCE_ONLY | lob::IOC, 4);
  EXPECT_FALSE(close.accepted);
  EXPECT_EQ(close.code, lobx::RejectCode::InsufficientBalance);
  EXPECT_EQ(f.exchange.trades().size(), trades_before);
  EXPECT_EQ(f.exchange.position(f.alice, f.perp_symbol).signed_qty, 50);
  require_invariants(f.exchange);
}

TEST(ExchangePerpConsistency, PerpFullCloseClearsPositionMargin) {
  auto f = ExchangeFixture::Perp();

  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.bob, 36021, lob::Side::Ask, 100, 4, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.alice, 36022, lob::Side::Bid, 100, 4, lob::IOC, 2).accepted);
  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.carol, 36023, lob::Side::Bid, 110, 4, lob::POST_ONLY, 3).accepted);
  auto close = f.exchange.submit_limit(f.perp_symbol, f.alice, 36024, lob::Side::Ask, 110, 4, lobx::LOBX_REDUCE_ONLY | lob::IOC, 4);

  EXPECT_TRUE_MSG(close.accepted, "user=alice symbol=BTC-USDT-PERP order_id=36024 side=SELL price=110 qty=4 flags=REDUCE_ONLY|IOC reason=" + close.reason);
  EXPECT_EQ(f.exchange.position(f.alice, f.perp_symbol).signed_qty, 0);
  EXPECT_EQ_MSG(f.exchange.balance(f.alice, "USDT").locked, 0, f.wallet_summary(f.alice));
  require_invariants(f.exchange);
}
