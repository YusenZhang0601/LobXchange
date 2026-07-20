#include "test_helpers/market_microstructure_helpers.hpp"
#include "test_helpers/replay_assertions.hpp"

using namespace lobx_test;

namespace {

int exchange_event_count(lobx::Exchange& exchange, const std::string& type) {
  return event_count(exchange.events(), type);
}

} // namespace

TEST(ExchangeReplayConsistency, CommittedTradeAppearsInTradeHistoryAndEventStore) {
  auto f = ExchangeFixture::Spot();
  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.alice, 52001, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);

  auto bid = f.exchange.submit_limit(f.spot_symbol, f.bob, 52002, lob::Side::Bid, 100, 1, lob::IOC, 2);

  EXPECT_TRUE(bid.accepted);
  EXPECT_EQ(f.exchange.trades().size(), 1UL);
  EXPECT_EQ(exchange_event_count(f.exchange, "trade"), 1);
  expect_trade_history_matches_trade_events(f.exchange);
}

TEST(ExchangeReplayConsistency, RejectedOrderDoesNotAppearInTradeHistory) {
  auto f = ExchangeFixture::Spot();
  const auto trades_before = f.exchange.trades().size();

  auto rejected = f.exchange.submit_limit(f.spot_symbol, f.alice, 52011, lob::Side::Ask, 100, 1, 1u << 30, 1);

  EXPECT_FALSE(rejected.accepted);
  EXPECT_EQ(f.exchange.trades().size(), trades_before);
  EXPECT_EQ(exchange_event_count(f.exchange, "trade"), 0);
  expect_no_later_trade_for_order(f.exchange.events(), 52011);
}

TEST(ExchangeReplayConsistency, ExpiredFOKDoesNotAppearInTradeHistory) {
  auto f = ExchangeFixture::Spot();

  auto expired = f.exchange.submit_limit(f.spot_symbol, f.bob, 52021, lob::Side::Bid, 100, 1, lob::FOK, 1);

  EXPECT_TRUE(expired.accepted);
  EXPECT_EQ(expired.exec.filled, 0);
  EXPECT_TRUE(f.exchange.trades().empty());
  EXPECT_EQ(exchange_event_count(f.exchange, "trade"), 0);
  EXPECT_EQ(exchange_event_count(f.exchange, "order.expired"), 1);
}

TEST(ExchangeReplayConsistency, TradeEventOrderMatchesExecutionOrder) {
  auto f = ExchangeFixture::Spot();
  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.alice, 52031, lob::Side::Ask, 90, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.carol, 52032, lob::Side::Ask, 95, 1, lob::POST_ONLY, 2).accepted);

  auto bid = f.exchange.submit_limit(f.spot_symbol, f.bob, 52033, lob::Side::Bid, 100, 2, lob::IOC, 3);

  EXPECT_TRUE(bid.accepted);
  EXPECT_EQ(f.exchange.trades().size(), 2UL);
  EXPECT_EQ(f.exchange.trades()[0].seller_order_id, 52031ULL);
  EXPECT_EQ(f.exchange.trades()[1].seller_order_id, 52032ULL);
}

TEST(ExchangeReplayConsistency, BookSnapshotMatchesOpenOrdersAfterSequence) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 52041, lob::Side::Ask, 100, 3, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.bob, 52042, lob::Side::Bid, 90, 2, lob::POST_ONLY, 2).accepted);
  EXPECT_TRUE(f.submit(f.carol, 52043, lob::Side::Bid, 100, 1, lob::IOC, 3).accepted);

  expect_topN_matches_open_orders(f.engine);
}

TEST(ExchangeReplayConsistency, BookTopNMatchesOpenOrdersAggregateAfterSequence) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 52051, lob::Side::Ask, 100, 2, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.carol, 52052, lob::Side::Ask, 101, 3, lob::POST_ONLY, 2).accepted);
  EXPECT_TRUE(f.submit(f.bob, 52053, lob::Side::Bid, 100, 1, lob::IOC, 3).accepted);

  EXPECT_EQ(top_qty(f.engine, lob::Side::Ask, 100), 1);
  EXPECT_EQ(top_qty(f.engine, lob::Side::Ask, 101), 3);
  expect_topN_matches_open_orders(f.engine);
}

TEST(ExchangeReplayConsistency, DrainTradesReturnsOnlyCommittedTrades) {
  auto f = ExchangeFixture::Spot();
  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.alice, 52061, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_FALSE(f.exchange.submit_limit(f.spot_symbol, f.bob, 52062, lob::Side::Bid, 100, 1, lob::POST_ONLY, 2).accepted);
  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.bob, 52063, lob::Side::Bid, 100, 1, lob::IOC, 3).accepted);

  auto trades = f.exchange.drain_trades();
  auto second = f.exchange.drain_trades();

  EXPECT_EQ(trades.size(), 1UL);
  EXPECT_TRUE(second.empty());
}

TEST(ExchangeReplayConsistency, DrainCandlesReturnsOnlyCommittedCandles) {
  auto f = ExchangeFixture::Spot();
  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.alice, 52071, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_FALSE(f.exchange.submit_limit(f.spot_symbol, f.bob, 52072, lob::Side::Bid, 100, 1, lob::POST_ONLY, 2).accepted);
  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.bob, 52073, lob::Side::Bid, 100, 1, lob::IOC, 3).accepted);
  f.exchange.flush_candles();

  auto candles = f.exchange.drain_candles();
  auto second = f.exchange.drain_candles();

  EXPECT_FALSE(candles.empty());
  EXPECT_EQ(candles.front().volume, 1);
  EXPECT_TRUE(second.empty());
}
