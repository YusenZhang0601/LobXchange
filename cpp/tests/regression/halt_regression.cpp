#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/invariant_checker.hpp"
#include "test_helpers/test_framework.hpp"

using namespace lobx_test;

TEST(HaltRegression, RegistryHaltRejectsNewOrdersAtEngine) {
  auto f = ExchangeFixture::Spot();

  auto halt = f.exchange.markets().halt(f.spot_market_id);
  EXPECT_TRUE_MSG(halt.ok, "symbol=BTC-USDT market_id=" + std::to_string(f.spot_market_id) + " reason=" + halt.reason);

  auto order = f.exchange.submit_limit(f.spot_symbol, f.alice, 3001, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1);
  EXPECT_FALSE_MSG(order.accepted, "user=alice symbol=BTC-USDT order_id=3001 side=SELL price=100 qty=1 flags=POST_ONLY reason=" + order.reason);
  EXPECT_EQ(order.code, lobx::RejectCode::MarketNotActive);
  require_invariants(f.exchange);
}

TEST(HaltRegression, ActivateAfterHaltRestoresOrderAcceptance) {
  auto f = ExchangeFixture::Spot();

  EXPECT_TRUE(f.exchange.markets().halt(f.spot_market_id).ok);
  EXPECT_TRUE(f.exchange.markets().activate(f.spot_market_id).ok);

  auto order = f.exchange.submit_limit(f.spot_symbol, f.alice, 3002, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1);
  EXPECT_TRUE_MSG(order.accepted, "user=alice symbol=BTC-USDT order_id=3002 side=SELL price=100 qty=1 flags=POST_ONLY reason=" + order.reason);
  require_invariants(f.exchange);
}

TEST(HaltRegression, HaltedMarketStillAllowsCancelOfExistingRestingOrders) {
  auto f = ExchangeFixture::Spot();

  auto order = f.exchange.submit_limit(f.spot_symbol, f.alice, 3003, lob::Side::Ask, 100, 5, lob::NONE, 1);
  EXPECT_TRUE_MSG(order.accepted, "user=alice symbol=BTC-USDT order_id=3003 side=SELL price=100 qty=5 flags=NONE reason=" + order.reason);
  EXPECT_EQ(f.exchange.balance(f.alice, "BTC").locked, 5);

  EXPECT_TRUE(f.exchange.markets().halt(f.spot_market_id).ok);
  EXPECT_TRUE_MSG(f.exchange.cancel(f.spot_symbol, 3003), "cancel should be available while market is halted for existing order_id=3003");
  EXPECT_EQ_MSG(f.exchange.balance(f.alice, "BTC").locked, 0, f.wallet_summary(f.alice));
  require_invariants(f.exchange);
}

TEST(HaltRegression, RegistryStateAndEngineExecutionStayConsistentAcrossStateChanges) {
  auto f = ExchangeFixture::Spot();

  EXPECT_TRUE(f.exchange.markets().halt(f.spot_market_id).ok);
  auto rejected = f.exchange.submit_limit(f.spot_symbol, f.alice, 3004, lob::Side::Bid, 100, 1, lob::NONE, 1);
  EXPECT_FALSE(rejected.accepted);
  EXPECT_EQ(rejected.code, lobx::RejectCode::MarketNotActive);

  EXPECT_TRUE(f.exchange.markets().activate(f.spot_market_id).ok);
  auto accepted = f.exchange.submit_limit(f.spot_symbol, f.alice, 3005, lob::Side::Bid, 100, 1, lob::NONE, 2);
  EXPECT_TRUE_MSG(accepted.accepted, "user=alice symbol=BTC-USDT order_id=3005 side=BUY price=100 qty=1 flags=NONE reason=" + accepted.reason);
  require_invariants(f.exchange);
}
