#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/invariant_checker.hpp"
#include "test_helpers/test_framework.hpp"

using namespace lobx_test;

TEST(MarketStateIntegration, ActiveMarketAcceptsOrders) {
  auto f = ExchangeFixture::Spot();

  auto order = f.exchange.submit_limit(f.spot_symbol, f.alice, 15001, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1);
  EXPECT_TRUE_MSG(order.accepted, "user=alice symbol=BTC-USDT order_id=15001 side=SELL price=100 qty=1 flags=POST_ONLY reason=" + order.reason);
  require_invariants(f.exchange);
}

TEST(MarketStateIntegration, HaltedMarketRejectsNewOrders) {
  auto f = ExchangeFixture::Spot();

  EXPECT_TRUE(f.exchange.markets().halt(f.spot_market_id).ok);
  auto order = f.exchange.submit_limit(f.spot_symbol, f.alice, 15002, lob::Side::Ask, 100, 1, lob::NONE, 1);
  EXPECT_FALSE(order.accepted);
  EXPECT_EQ(order.code, lobx::RejectCode::MarketNotActive);
  require_invariants(f.exchange);
}

TEST(MarketStateIntegration, ActivateAfterHaltRestoresTrading) {
  auto f = ExchangeFixture::Spot();

  EXPECT_TRUE(f.exchange.markets().halt(f.spot_market_id).ok);
  EXPECT_TRUE(f.exchange.markets().activate(f.spot_market_id).ok);
  auto order = f.exchange.submit_limit(f.spot_symbol, f.alice, 15003, lob::Side::Bid, 100, 1, lob::NONE, 1);
  EXPECT_TRUE_MSG(order.accepted, "user=alice symbol=BTC-USDT order_id=15003 side=BUY price=100 qty=1 flags=NONE reason=" + order.reason);
  require_invariants(f.exchange);
}

TEST(MarketStateIntegration, PostOnlyRejectsOrdersThatWouldImmediatelyTrade) {
  auto f = ExchangeFixture::Spot();

  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.alice, 15004, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  auto cross = f.exchange.submit_limit(f.spot_symbol, f.bob, 15005, lob::Side::Bid, 100, 1, lob::POST_ONLY, 2);
  EXPECT_FALSE(cross.accepted);
  EXPECT_EQ(cross.code, lobx::RejectCode::PostOnlyWouldCross);
  require_invariants(f.exchange);
}

TEST(MarketStateIntegration, ReduceOnlyOnPerpOnlyAllowsRiskReducingOrders) {
  auto f = ExchangeFixture::Perp();

  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.bob, 15011, lob::Side::Ask, 100, 4, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.alice, 15012, lob::Side::Bid, 100, 4, lob::IOC, 2).accepted);

  auto risk_increasing = f.exchange.submit_limit(f.perp_symbol, f.alice, 15013, lob::Side::Bid, 100, 1, lobx::LOBX_REDUCE_ONLY | lob::IOC, 3);
  EXPECT_FALSE(risk_increasing.accepted);
  EXPECT_EQ(risk_increasing.code, lobx::RejectCode::ReduceOnlyWouldIncrease);

  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.carol, 15014, lob::Side::Bid, 100, 2, lob::POST_ONLY, 4).accepted);
  auto reducing = f.exchange.submit_limit(f.perp_symbol, f.alice, 15015, lob::Side::Ask, 100, 2, lobx::LOBX_REDUCE_ONLY | lob::IOC, 5);
  EXPECT_TRUE_MSG(reducing.accepted, "user=alice symbol=BTC-USDT-PERP order_id=15015 side=SELL price=100 qty=2 flags=REDUCE_ONLY|IOC reason=" + reducing.reason);
  EXPECT_EQ(f.exchange.position(f.alice, f.perp_symbol).signed_qty, 2);
  require_invariants(f.exchange);
}
