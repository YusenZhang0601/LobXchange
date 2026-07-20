#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/invariant_checker.hpp"
#include "test_helpers/test_framework.hpp"

using namespace lobx_test;

TEST(SpotMatchingIntegration, SingleLevelTradeUpdatesBookWalletAndLocks) {
  auto f = ExchangeFixture::Spot();

  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.alice, 11001, lob::Side::Ask, 100, 5, lob::NONE, 1).accepted);
  auto bid = f.exchange.submit_limit(f.spot_symbol, f.bob, 11002, lob::Side::Bid, 100, 5, lob::IOC, 2);

  EXPECT_TRUE_MSG(bid.accepted, "user=bob symbol=BTC-USDT order_id=11002 side=BUY price=100 qty=5 flags=IOC reason=" + bid.reason);
  EXPECT_EQ(bid.exec.filled, 5);
  EXPECT_TRUE(f.exchange.topN(f.spot_symbol, lob::Side::Ask, 10).empty());
  EXPECT_EQ_MSG(f.exchange.balance(f.bob, "BTC").total, 1000005LL, f.wallet_summary(f.bob));
  EXPECT_EQ_MSG(f.exchange.balance(f.alice, "BTC").locked, 0, f.wallet_summary(f.alice));
  require_invariants(f.exchange);
}

TEST(SpotMatchingIntegration, MultiLevelTradeConsumesBestPricesFirst) {
  auto f = ExchangeFixture::Spot();

  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.alice, 11011, lob::Side::Ask, 99, 2, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.carol, 11012, lob::Side::Ask, 100, 3, lob::POST_ONLY, 2).accepted);
  auto bid = f.exchange.submit_limit(f.spot_symbol, f.bob, 11013, lob::Side::Bid, 100, 4, lob::IOC, 3);

  EXPECT_TRUE_MSG(bid.accepted, "user=bob symbol=BTC-USDT order_id=11013 side=BUY price=100 qty=4 flags=IOC reason=" + bid.reason);
  EXPECT_EQ(bid.exec.filled, 4);
  EXPECT_EQ(bid.trades.size(), 2UL);
  EXPECT_EQ(bid.trades[0].price, 99);
  EXPECT_EQ(bid.trades[1].price, 100);
  auto asks = f.exchange.topN(f.spot_symbol, lob::Side::Ask, 10);
  EXPECT_FALSE(asks.empty());
  EXPECT_EQ(asks[0].first, 100);
  EXPECT_EQ(asks[0].second, 1);
  require_invariants(f.exchange);
}

TEST(SpotMatchingIntegration, PartialFillLeavesRestingQuantity) {
  auto f = ExchangeFixture::Spot();

  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.alice, 11021, lob::Side::Ask, 100, 5, lob::NONE, 1).accepted);
  auto bid = f.exchange.submit_limit(f.spot_symbol, f.bob, 11022, lob::Side::Bid, 100, 2, lob::IOC, 2);

  EXPECT_TRUE(bid.accepted);
  EXPECT_EQ(bid.exec.filled, 2);
  auto asks = f.exchange.topN(f.spot_symbol, lob::Side::Ask, 10);
  EXPECT_FALSE(asks.empty());
  EXPECT_EQ(asks[0].second, 3);
  EXPECT_EQ_MSG(f.exchange.balance(f.alice, "BTC").locked, 3, f.wallet_summary(f.alice));
  require_invariants(f.exchange);
}

TEST(SpotMatchingIntegration, PriceImprovementUsesMakerPrice) {
  auto f = ExchangeFixture::Spot();

  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.alice, 11031, lob::Side::Ask, 90, 5, lob::POST_ONLY, 1).accepted);
  auto bid = f.exchange.submit_limit(f.spot_symbol, f.bob, 11032, lob::Side::Bid, 100, 5, lob::IOC, 2);

  EXPECT_TRUE(bid.accepted);
  EXPECT_EQ(bid.trades.size(), 1UL);
  EXPECT_EQ(bid.trades[0].price, 90);
  EXPECT_EQ_MSG(f.exchange.balance(f.bob, "USDT").total, 1000000LL - 450LL, f.wallet_summary(f.bob));
  require_invariants(f.exchange);
}

TEST(SpotMatchingIntegration, MakerAndTakerSidesAreRecordedOnTrade) {
  auto f = ExchangeFixture::Spot();

  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.alice, 11041, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  auto bid = f.exchange.submit_limit(f.spot_symbol, f.bob, 11042, lob::Side::Bid, 100, 1, lob::IOC, 2);

  EXPECT_TRUE(bid.accepted);
  EXPECT_EQ(bid.trades.size(), 1UL);
  EXPECT_EQ(bid.trades[0].buyer, f.bob);
  EXPECT_EQ(bid.trades[0].seller, f.alice);
  EXPECT_EQ(bid.trades[0].liquidity_side, lob::Side::Ask);
  require_invariants(f.exchange);
}
