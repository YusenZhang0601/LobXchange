#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/invariant_checker.hpp"
#include "test_helpers/test_framework.hpp"

using namespace lobx_test;

TEST(CancelLifecycleIntegration, OwnerCancelReleasesRestingOrderLock) {
  auto f = ExchangeFixture::Spot();

  auto ask = f.exchange.submit_limit(f.spot_symbol, f.alice, 14001, lob::Side::Ask, 100, 5, lob::NONE, 1);
  EXPECT_TRUE_MSG(ask.accepted, "user=alice symbol=BTC-USDT order_id=14001 side=SELL price=100 qty=5 flags=NONE reason=" + ask.reason);

  EXPECT_TRUE(f.exchange.cancel(f.spot_symbol, 14001));
  EXPECT_EQ_MSG(f.exchange.balance(f.alice, "BTC").locked, 0, f.wallet_summary(f.alice));
  EXPECT_TRUE(f.exchange.topN(f.spot_symbol, lob::Side::Ask, 10).empty());
  require_invariants(f.exchange);
}

TEST(CancelLifecycleIntegration, CancelUnknownOrderReturnsNotFound) {
  auto f = ExchangeFixture::Spot();

  EXPECT_FALSE_MSG(f.exchange.cancel(f.spot_symbol, 14999), "symbol=BTC-USDT order_id=14999 should not exist");
  require_invariants(f.exchange);
}

TEST(CancelLifecycleIntegration, CancelFilledOrderIsNoopNotFound) {
  auto f = ExchangeFixture::Spot();

  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.alice, 14011, lob::Side::Ask, 100, 2, lob::POST_ONLY, 1).accepted);
  auto bid = f.exchange.submit_limit(f.spot_symbol, f.bob, 14012, lob::Side::Bid, 100, 2, lob::IOC, 2);
  EXPECT_TRUE(bid.accepted);
  EXPECT_EQ(bid.exec.filled, 2);

  EXPECT_FALSE_MSG(f.exchange.cancel(f.spot_symbol, 14011), "filled order_id=14011 should no longer be open");
  require_invariants(f.exchange);
}

TEST(CancelLifecycleIntegration, CancelPartiallyFilledOrderReleasesOnlyRemainingLock) {
  auto f = ExchangeFixture::Spot();

  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.alice, 14021, lob::Side::Ask, 100, 5, lob::NONE, 1).accepted);
  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.bob, 14022, lob::Side::Bid, 100, 2, lob::IOC, 2).accepted);
  EXPECT_EQ_MSG(f.exchange.balance(f.alice, "BTC").locked, 3, f.wallet_summary(f.alice));

  EXPECT_TRUE(f.exchange.cancel(f.spot_symbol, 14021));
  EXPECT_EQ_MSG(f.exchange.balance(f.alice, "BTC").locked, 0, f.wallet_summary(f.alice));
  EXPECT_EQ_MSG(f.exchange.balance(f.alice, "BTC").total, 1000000LL - 2LL, f.wallet_summary(f.alice));
  require_invariants(f.exchange);
}

TEST(CancelLifecycleIntegration, CancelAfterStpDoesNotDoubleRelease) {
  SpotEngineFixture f;

  EXPECT_TRUE(f.submit(f.alice, 14031, lob::Side::Ask, 100, 5, lob::NONE, 1).accepted);
  EXPECT_EQ(f.ledger.locked(f.alice, f.base_asset), 5);

  auto stp = f.submit(f.alice, 14032, lob::Side::Bid, 100, 5, lob::IOC | lob::STP, 2);
  EXPECT_TRUE_MSG(stp.accepted, "user=alice symbol=BTC-USDT order_id=14032 side=BUY price=100 qty=5 flags=IOC|STP reason=" + stp.reason);
  EXPECT_EQ(f.ledger.locked(f.alice, f.base_asset), 0);

  EXPECT_FALSE(f.engine.cancel(14031));
  EXPECT_EQ_MSG(f.ledger.locked(f.alice, f.base_asset), 0, f.ledger_summary(f.alice));
  auto report = check_order_book_invariants(f.engine, f.ledger);
  EXPECT_TRUE_MSG(report.ok, report_to_string(report));
}

TEST(CancelLifecycleIntegration, OrderLifecycleMovesFromOpenToPartialToFilledOrCanceled) {
  auto f = ExchangeFixture::Spot();

  auto ask = f.exchange.submit_limit(f.spot_symbol, f.alice, 14041, lob::Side::Ask, 100, 5, lob::NONE, 1);
  EXPECT_TRUE(ask.accepted);
  EXPECT_EQ(ask.exec.remaining, 5);
  EXPECT_FALSE(f.exchange.topN(f.spot_symbol, lob::Side::Ask, 10).empty());

  auto partial = f.exchange.submit_limit(f.spot_symbol, f.bob, 14042, lob::Side::Bid, 100, 2, lob::IOC, 2);
  EXPECT_TRUE(partial.accepted);
  EXPECT_EQ(partial.exec.filled, 2);
  EXPECT_EQ(f.exchange.topN(f.spot_symbol, lob::Side::Ask, 10)[0].second, 3);

  EXPECT_TRUE(f.exchange.cancel(f.spot_symbol, 14041));
  EXPECT_TRUE(f.exchange.topN(f.spot_symbol, lob::Side::Ask, 10).empty());
  EXPECT_FALSE(f.exchange.cancel(f.spot_symbol, 14041));
  require_invariants(f.exchange);
}
