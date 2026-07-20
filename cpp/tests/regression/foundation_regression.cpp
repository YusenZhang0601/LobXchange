#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/invariant_checker.hpp"
#include "test_helpers/test_framework.hpp"

using namespace lobx_test;

TEST(FoundationRegression, SpotPriceImprovementSettlesAtMakerPriceAndReleasesOverLock) {
  auto f = ExchangeFixture::Spot();

  auto ask = f.exchange.submit_limit(f.spot_symbol, f.alice, 1001, lob::Side::Ask, 90, 5, lob::POST_ONLY, 1);
  EXPECT_TRUE_MSG(ask.accepted, "user=alice symbol=BTC-USDT order_id=1001 side=SELL price=90 qty=5 flags=POST_ONLY reason=" + ask.reason);

  auto bid = f.exchange.submit_limit(f.spot_symbol, f.bob, 1002, lob::Side::Bid, 100, 5, lob::NONE, 2);
  EXPECT_TRUE_MSG(bid.accepted, "user=bob symbol=BTC-USDT order_id=1002 side=BUY price=100 qty=5 flags=NONE reason=" + bid.reason);
  EXPECT_EQ(bid.exec.filled, 5);
  EXPECT_EQ(bid.exec.remaining, 0);
  EXPECT_EQ(f.exchange.trades().back().price, 90);
  EXPECT_EQ_MSG(f.exchange.balance(f.bob, "USDT").locked, 0, f.wallet_summary(f.bob));
  EXPECT_EQ_MSG(f.exchange.balance(f.bob, "USDT").total, 1000000LL - 450LL, f.wallet_summary(f.bob));
  require_invariants(f.exchange);
}

TEST(FoundationRegression, RestingLockIsReleasedAfterPartialFillAndCancel) {
  auto f = ExchangeFixture::Spot();

  auto ask = f.exchange.submit_limit(f.spot_symbol, f.alice, 1011, lob::Side::Ask, 100, 5, lob::NONE, 1);
  EXPECT_TRUE_MSG(ask.accepted, "user=alice symbol=BTC-USDT order_id=1011 side=SELL price=100 qty=5 flags=NONE reason=" + ask.reason);

  auto bid = f.exchange.submit_limit(f.spot_symbol, f.bob, 1012, lob::Side::Bid, 100, 2, lob::IOC, 2);
  EXPECT_TRUE_MSG(bid.accepted, "user=bob symbol=BTC-USDT order_id=1012 side=BUY price=100 qty=2 flags=IOC reason=" + bid.reason);
  EXPECT_EQ_MSG(f.exchange.balance(f.alice, "BTC").locked, 3, f.wallet_summary(f.alice));

  EXPECT_TRUE_MSG(f.exchange.cancel(f.spot_symbol, 1011), "user=alice symbol=BTC-USDT order_id=1011 cancel remaining ask");
  EXPECT_EQ_MSG(f.exchange.balance(f.alice, "BTC").locked, 0, f.wallet_summary(f.alice));
  EXPECT_TRUE(f.exchange.topN(f.spot_symbol, lob::Side::Ask, 10).empty());
  require_invariants(f.exchange);
}

TEST(FoundationRegression, AccountingInvariantGuardSurvivesBasicSpotFlow) {
  auto f = ExchangeFixture::Spot();

  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.alice, 1021, lob::Side::Ask, 100, 4, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.bob, 1022, lob::Side::Bid, 100, 2, lob::IOC, 2).accepted);
  EXPECT_TRUE(f.exchange.cancel(f.spot_symbol, 1021));

  auto report = check_accounting_invariants(f.exchange);
  EXPECT_TRUE_MSG(report.ok, report_to_string(report));
}

TEST(FoundationRegression, UnsupportedFlagBitsAreRejectedRegressionGuard) {
  auto f = ExchangeFixture::Spot();

  constexpr uint32_t unknown_flag = 1u << 30;
  auto order = f.exchange.submit_limit(f.spot_symbol, f.alice, 1025, lob::Side::Ask, 100, 1, unknown_flag, 1);
  EXPECT_FALSE_MSG(order.accepted, "user=alice symbol=BTC-USDT order_id=1025 side=SELL price=100 qty=1 flags=UNKNOWN reason=" + order.reason);
  EXPECT_EQ(order.code, lobx::RejectCode::UnsupportedOrderType);
  require_invariants(f.exchange);
}

TEST(FoundationRegression, TestFrameworkSanityCheckReportsPassingAssertions) {
  EXPECT_TRUE(true);
  EXPECT_EQ(1 + 1, 2);
}

TEST(FoundationRegression, FailedAssetIssueDoesNotReserveSymbolForever) {
  lobx::Exchange ex;
  lobx::AssetId bad_id = 0;

  auto failed = ex.issue_asset("BAD", 0, 10, 1, 11, &bad_id);
  EXPECT_FALSE(failed.ok);

  auto retry = ex.issue_asset("BAD", 0, 10, 1, 0, &bad_id);
  EXPECT_TRUE_MSG(retry.ok, "symbol=BAD retry after failed issue reason=" + retry.reason);
}
