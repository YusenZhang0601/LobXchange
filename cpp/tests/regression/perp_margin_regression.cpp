#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/invariant_checker.hpp"
#include "test_helpers/test_framework.hpp"

using namespace lobx_test;

TEST(PerpMarginRegression, IocOpenMovesOrderMarginToPositionMargin) {
  auto f = ExchangeFixture::Perp();

  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.bob, 4001, lob::Side::Ask, 100, 10, lob::POST_ONLY, 1).accepted);
  auto open = f.exchange.submit_limit(f.perp_symbol, f.alice, 4002, lob::Side::Bid, 100, 4, lob::IOC, 2);

  EXPECT_TRUE_MSG(open.accepted, "user=alice symbol=BTC-USDT-PERP order_id=4002 side=BUY price=100 qty=4 flags=IOC reason=" + open.reason);
  EXPECT_EQ(open.exec.filled, 4);
  EXPECT_EQ(f.exchange.position(f.alice, f.perp_symbol).signed_qty, 4);
  EXPECT_EQ_MSG(f.exchange.balance(f.alice, "USDT").locked, 80, f.wallet_summary(f.alice));
  require_invariants(f.exchange);
}

TEST(PerpMarginRegression, OpenPositionAlwaysHasVisiblePositionMargin) {
  auto f = ExchangeFixture::Perp();

  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.bob, 4011, lob::Side::Ask, 100, 10, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.alice, 4012, lob::Side::Bid, 100, 4, lob::IOC, 2).accepted);

  EXPECT_EQ(f.exchange.position(f.alice, f.perp_symbol).signed_qty, 4);
  EXPECT_EQ(f.exchange.position(f.bob, f.perp_symbol).signed_qty, -4);
  EXPECT_TRUE_MSG(f.exchange.balance(f.alice, "USDT").locked > 0, f.wallet_summary(f.alice));
  EXPECT_TRUE_MSG(f.exchange.balance(f.bob, "USDT").locked > 0, f.wallet_summary(f.bob));
  require_invariants(f.exchange);
}

TEST(PerpMarginRegression, PartialFillKeepsMakerRemainingOrderMarginAndPositionMargin) {
  auto f = ExchangeFixture::Perp();

  auto ask = f.exchange.submit_limit(f.perp_symbol, f.bob, 4021, lob::Side::Ask, 100, 10, lob::NONE, 1);
  EXPECT_TRUE_MSG(ask.accepted, "user=bob symbol=BTC-USDT-PERP order_id=4021 side=SELL price=100 qty=10 flags=NONE reason=" + ask.reason);
  EXPECT_EQ_MSG(f.exchange.balance(f.bob, "USDT").locked, 200, f.wallet_summary(f.bob));

  auto open = f.exchange.submit_limit(f.perp_symbol, f.alice, 4022, lob::Side::Bid, 100, 4, lob::IOC, 2);
  EXPECT_TRUE_MSG(open.accepted, "user=alice symbol=BTC-USDT-PERP order_id=4022 side=BUY price=100 qty=4 flags=IOC reason=" + open.reason);
  EXPECT_EQ(open.exec.filled, 4);
  EXPECT_EQ(f.exchange.position(f.bob, f.perp_symbol).signed_qty, -4);
  EXPECT_EQ_MSG(f.exchange.balance(f.bob, "USDT").locked, 200, f.wallet_summary(f.bob));

  auto asks = f.exchange.topN(f.perp_symbol, lob::Side::Ask, 10);
  EXPECT_FALSE(asks.empty());
  EXPECT_EQ(asks[0].second, 6);
  require_invariants(f.exchange);
}

TEST(PerpMarginRegression, IocRemainderReleaseDoesNotReleaseFilledPositionMargin) {
  auto f = ExchangeFixture::Perp();

  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.bob, 4031, lob::Side::Ask, 100, 10, lob::POST_ONLY, 1).accepted);
  auto open = f.exchange.submit_limit(f.perp_symbol, f.alice, 4032, lob::Side::Bid, 100, 12, lob::IOC, 2);

  EXPECT_TRUE_MSG(open.accepted, "user=alice symbol=BTC-USDT-PERP order_id=4032 side=BUY price=100 qty=12 flags=IOC reason=" + open.reason);
  EXPECT_EQ(open.exec.filled, 10);
  EXPECT_EQ(open.exec.remaining, 2);
  EXPECT_EQ(f.exchange.position(f.alice, f.perp_symbol).signed_qty, 10);
  EXPECT_EQ_MSG(f.exchange.balance(f.alice, "USDT").locked, 200, f.wallet_summary(f.alice));
  require_invariants(f.exchange);
}
