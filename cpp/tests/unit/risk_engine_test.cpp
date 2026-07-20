#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/test_framework.hpp"

using namespace lobx_test;

TEST(RiskEngineTest, SpotBuyLocksQuoteAndCancelReleasesIt) {
  auto f = ExchangeFixture::Spot();
  auto order = f.exchange.submit_limit(f.spot_symbol, f.alice, 100, lob::Side::Bid, 100, 5, lob::NONE, 1);
  EXPECT_TRUE(order.accepted);
  EXPECT_EQ(f.exchange.balance(f.alice, "USDT").locked, 500);
  EXPECT_TRUE(f.exchange.cancel(f.spot_symbol, 100));
  EXPECT_EQ(f.exchange.balance(f.alice, "USDT").locked, 0);
}

TEST(RiskEngineTest, SpotSellLocksBaseAndPartialFillLeavesRemainingLock) {
  auto f = ExchangeFixture::Spot();
  auto ask = f.exchange.submit_limit(f.spot_symbol, f.alice, 101, lob::Side::Ask, 100, 5, lob::NONE, 1);
  EXPECT_TRUE(ask.accepted);
  EXPECT_EQ(f.exchange.balance(f.alice, "BTC").locked, 5);
  auto bid = f.exchange.submit_limit(f.spot_symbol, f.bob, 102, lob::Side::Bid, 100, 2, lob::IOC, 2);
  EXPECT_TRUE(bid.accepted);
  EXPECT_EQ(f.exchange.balance(f.alice, "BTC").locked, 3);
}

TEST(RiskEngineTest, PriceImprovementReleasesOverLockedQuote) {
  auto f = ExchangeFixture::Spot();
  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.alice, 103, lob::Side::Ask, 90, 5, lob::POST_ONLY, 1).accepted);
  auto bid = f.exchange.submit_limit(f.spot_symbol, f.bob, 104, lob::Side::Bid, 100, 5, lob::NONE, 2);
  EXPECT_TRUE(bid.accepted);
  EXPECT_EQ(f.exchange.balance(f.bob, "USDT").locked, 0);
}

TEST(RiskEngineTest, PerpOpenSeparatesOrderMarginAndPositionMarginExpectation) {
  auto f = ExchangeFixture::Perp();
  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.bob, 105, lob::Side::Ask, 100, 10, lob::POST_ONLY, 1).accepted);
  auto buy = f.exchange.submit_limit(f.perp_symbol, f.alice, 106, lob::Side::Bid, 100, 4, lob::IOC, 2);
  EXPECT_TRUE(buy.accepted);
  EXPECT_EQ(f.exchange.position(f.alice, f.perp_symbol).signed_qty, 4);
  EXPECT_TRUE(f.exchange.balance(f.alice, "USDT").locked > 0);
}
