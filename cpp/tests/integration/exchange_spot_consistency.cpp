#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/invariant_checker.hpp"
#include "test_helpers/test_framework.hpp"

using namespace lobx_test;

namespace {

int count_events(const lobx::Exchange& exchange, const std::string& type) {
  int count = 0;
  for (const auto& event : const_cast<lobx::Exchange&>(exchange).events().records()) {
    if (event.type == type) ++count;
  }
  return count;
}

} // namespace

TEST(ExchangeSpotConsistency, TradeHistoryAndTradeEventsMatchCommittedTrades) {
  auto f = ExchangeFixture::Spot();

  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.alice, 35001, lob::Side::Ask, 100, 2, lob::POST_ONLY, 1).accepted);
  auto buy = f.exchange.submit_limit(f.spot_symbol, f.bob, 35002, lob::Side::Bid, 100, 2, lob::IOC, 2);

  EXPECT_TRUE_MSG(buy.accepted, "user=bob symbol=BTC-USDT order_id=35002 side=BUY price=100 qty=2 flags=IOC reason=" + buy.reason);
  EXPECT_EQ(f.exchange.trades().size(), 1UL);
  EXPECT_EQ(count_events(f.exchange, "trade"), 1);
  require_invariants(f.exchange);
}

TEST(ExchangeSpotConsistency, CandlesOnlyGeneratedFromCommittedTrades) {
  auto f = ExchangeFixture::Spot();

  auto no_liquidity_ioc = f.exchange.submit_limit(f.spot_symbol, f.bob, 35011, lob::Side::Bid, 100, 1, lob::IOC, 1);
  EXPECT_TRUE(no_liquidity_ioc.accepted);
  EXPECT_EQ(no_liquidity_ioc.exec.filled, 0);
  EXPECT_TRUE(f.exchange.trades().empty());
  EXPECT_TRUE(f.exchange.flush_candles().empty());

  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.alice, 35012, lob::Side::Ask, 100, 1, lob::POST_ONLY, 2).accepted);
  auto buy = f.exchange.submit_limit(f.spot_symbol, f.bob, 35013, lob::Side::Bid, 100, 1, lob::IOC, 3);
  EXPECT_TRUE(buy.accepted);
  EXPECT_FALSE(f.exchange.flush_candles().empty());
}

TEST(ExchangeSpotConsistency, RejectedOrderDoesNotCreateTradeHistoryOrKline) {
  auto f = ExchangeFixture::Spot();

  auto rejected = f.exchange.submit_limit(f.spot_symbol, f.alice, 35021, lob::Side::Ask, 100, 1, 1u << 30, 1);
  EXPECT_FALSE(rejected.accepted);
  EXPECT_TRUE(f.exchange.trades().empty());
  EXPECT_TRUE(f.exchange.flush_candles().empty());
  EXPECT_EQ(count_events(f.exchange, "trade"), 0);
  require_invariants(f.exchange);
}

TEST(ExchangeSpotConsistency, DuplicateOrderIdDoesNotDoubleLockOrCreateTrade) {
  auto f = ExchangeFixture::Spot();

  auto first = f.exchange.submit_limit(f.spot_symbol, f.alice, 35031, lob::Side::Ask, 100, 2, lob::NONE, 1);
  EXPECT_TRUE(first.accepted);
  const auto locked_before = f.exchange.balance(f.alice, "BTC").locked;

  auto duplicate = f.exchange.submit_limit(f.spot_symbol, f.alice, 35031, lob::Side::Ask, 100, 2, lob::NONE, 2);
  EXPECT_FALSE(duplicate.accepted);
  EXPECT_EQ(duplicate.code, lobx::RejectCode::DuplicateOrderId);
  EXPECT_EQ_MSG(f.exchange.balance(f.alice, "BTC").locked, locked_before, f.wallet_summary(f.alice));
  EXPECT_TRUE(f.exchange.trades().empty());
  require_invariants(f.exchange);
}
