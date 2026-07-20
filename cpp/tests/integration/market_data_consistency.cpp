#include "test_helpers/market_microstructure_helpers.hpp"
#include "test_helpers/replay_assertions.hpp"

using namespace lobx_test;

namespace {

bool event_type_after(const lobx::EventStore& events, const std::string& first, const std::string& later) {
  bool seen_first = false;
  for (const auto& event : events.records()) {
    if (event.type == first) seen_first = true;
    if (seen_first && event.type == later) return true;
  }
  return false;
}

} // namespace

TEST(MarketDataConsistency, BookSnapshotMatchesEngineTopN) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 53001, lob::Side::Ask, 100, 2, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.bob, 53002, lob::Side::Bid, 90, 3, lob::POST_ONLY, 2).accepted);

  expect_topN_matches_open_orders(f.engine);
}

TEST(MarketDataConsistency, BookSnapshotAfterPartialFillMatchesOpenOrders) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 53011, lob::Side::Ask, 100, 5, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.bob, 53012, lob::Side::Bid, 100, 2, lob::IOC, 2).accepted);

  EXPECT_EQ(top_qty(f.engine, lob::Side::Ask, 100), 3);
  expect_topN_matches_open_orders(f.engine);
}

TEST(MarketDataConsistency, BookSnapshotAfterCancelMatchesOpenOrders) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 53021, lob::Side::Ask, 100, 5, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.engine.cancel(53021, f.alice, 2));

  EXPECT_TRUE(f.engine.topN(lob::Side::Ask, 10).empty());
  expect_topN_matches_open_orders(f.engine);
}

TEST(MarketDataConsistency, TradeEventsOnlyForCommittedTrades) {
  auto f = ExchangeFixture::Spot();
  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.alice, 53031, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_FALSE(f.exchange.submit_limit(f.spot_symbol, f.bob, 53032, lob::Side::Bid, 100, 1, lob::POST_ONLY, 2).accepted);
  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.bob, 53033, lob::Side::Bid, 100, 1, lob::IOC, 3).accepted);

  EXPECT_EQ(f.exchange.trades().size(), 1UL);
  EXPECT_EQ(committed_trade_event_count(f.exchange.events()), 1);
}

TEST(MarketDataConsistency, TradeEventsContainBuyerSellerOrderIds) {
  auto f = ExchangeFixture::Spot();
  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.alice, 53041, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.bob, 53042, lob::Side::Bid, 100, 1, lob::IOC, 2).accepted);

  const auto trades = events_of_type(f.exchange.events(), "trade");

  EXPECT_EQ(trades.size(), 1UL);
  EXPECT_TRUE(payload_contains(trades.front(), "buyer_order=53042"));
  EXPECT_TRUE(payload_contains(trades.front(), "seller_order=53041"));
}

TEST(MarketDataConsistency, TradeEventsContainExecutionPriceAndQty) {
  auto f = ExchangeFixture::Spot();
  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.alice, 53051, lob::Side::Ask, 90, 2, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.bob, 53052, lob::Side::Bid, 100, 1, lob::IOC, 2).accepted);

  const auto trades = events_of_type(f.exchange.events(), "trade");

  EXPECT_EQ(trades.size(), 1UL);
  EXPECT_TRUE(payload_contains(trades.front(), "price=90"));
  EXPECT_TRUE(payload_contains(trades.front(), "qty=1"));
}

TEST(MarketDataConsistency, OrderAcceptedEventPrecedesTradeEvents) {
  auto f = ExchangeFixture::Spot();
  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.alice, 53061, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.bob, 53062, lob::Side::Bid, 100, 1, lob::IOC, 2).accepted);

  EXPECT_TRUE(event_type_after(f.exchange.events(), "order.accepted", "trade"));
}

TEST(MarketDataConsistency, OrderRejectedEventHasNoLaterTradeForSameOrder) {
  auto f = ExchangeFixture::Spot();

  auto rejected = f.exchange.submit_limit(f.spot_symbol, f.bob, 53071, lob::Side::Bid, 100, 1, 1u << 30, 1);

  EXPECT_FALSE(rejected.accepted);
  expect_no_later_trade_for_order(f.exchange.events(), 53071);
}

TEST(MarketDataConsistency, CanceledOrderHasCancelEventOnlyAfterCommit) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 53081, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  const int before = event_count(f.events, "order.canceled");

  EXPECT_TRUE(f.engine.cancel(53081, f.alice, 2));

  EXPECT_EQ(event_count(f.events, "order.canceled"), before + 1);
  EXPECT_FALSE(has_open_order(f.engine, 53081));
}
