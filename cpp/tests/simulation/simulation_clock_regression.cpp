#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/simulation_replay_helpers.hpp"
#include "test_helpers/simulation_test_harness.hpp"
#include "test_helpers/test_market_data_feed.hpp"
#include "test_helpers/test_framework.hpp"

#include <type_traits>
#include <vector>

using namespace lobx_test;

namespace {

bool has_open_order(const SpotEngineFixture& f, lobx::OrderId id) {
  for (const auto& order : f.engine.open_orders()) {
    if (order.id == id) return true;
  }
  return false;
}

lobx::TradeEvent committed_spot_trade_at_100(SpotEngineFixture& f) {
  EXPECT_TRUE(f.submit(f.alice, 56301, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  auto buy = f.submit(f.bob, 56302, lob::Side::Bid, 100, 1, lob::IOC, 100);
  EXPECT_TRUE_MSG(buy.accepted, buy.reason);
  EXPECT_EQ(buy.trades.size(), 1UL);
  return buy.trades.front();
}

} // namespace

TEST(SimulationClockRegression, SimulationClockMonotonic) {
  TestSimulationClock clock;

  EXPECT_TRUE(clock.advance_to(10));
  EXPECT_FALSE(clock.advance_to(9));
  EXPECT_EQ(clock.now, 10);
}

TEST(SimulationClockRegression, SameTimestampEventsUseDeterministicSequence) {
  TestEventQueue queue;
  queue.push(10, "first");
  queue.push(10, "second");

  EXPECT_EQ(queue.pop().type, std::string("first"));
  EXPECT_EQ(queue.pop().type, std::string("second"));
}

TEST(SimulationClockRegression, EventQueueProcessesByTimeThenSequence) {
  TestEventQueue queue;
  queue.push(20, "late");
  queue.push(10, "early-a");
  queue.push(10, "early-b");

  EXPECT_EQ(queue.pop().type, std::string("early-a"));
  EXPECT_EQ(queue.pop().type, std::string("early-b"));
  EXPECT_EQ(queue.pop().type, std::string("late"));
}

TEST(SimulationClockRegression, EventQueueStableForManySameTimestampEvents) {
  TestEventQueue queue;
  for (int i = 0; i < 100; ++i) queue.push(10, "evt-" + std::to_string(i));

  for (int i = 0; i < 100; ++i) {
    EXPECT_EQ(queue.pop().type, "evt-" + std::to_string(i));
  }
  EXPECT_TRUE(queue.empty());
}

TEST(SimulationClockRegression, EventQueueEmptyStateIsExplicit) {
  TestEventQueue queue;

  EXPECT_TRUE(queue.empty());
}

TEST(SimulationClockRegression, LowerLatencyOrderArrivesFirst) {
  TestLatencyModel fast{1, 1, 1};
  TestLatencyModel slow{5, 1, 1};

  EXPECT_TRUE(fast.order_arrival(100) < slow.order_arrival(100));
}

TEST(SimulationClockRegression, CancelLatencyCanMissAlreadyExecutedOrder) {
  SpotEngineFixture f;
  TestLatencyModel latency{/*order_latency=*/1, /*cancel_latency=*/10, /*market_data_latency=*/1};
  const auto alice_base_before = f.ledger.balance(f.alice, f.base_asset);
  const auto alice_quote_before = f.ledger.balance(f.alice, f.quote_asset);
  const auto bob_base_before = f.ledger.balance(f.bob, f.base_asset);
  const auto bob_quote_before = f.ledger.balance(f.bob, f.quote_asset);

  EXPECT_TRUE(f.submit(f.alice, 56001, lob::Side::Ask, 100, 1, lob::POST_ONLY, latency.order_arrival(1)).accepted);
  auto buy = f.submit(f.bob, 56002, lob::Side::Bid, 100, 1, lob::IOC, latency.order_arrival(2));
  EXPECT_TRUE_MSG(buy.accepted, buy.reason);
  EXPECT_EQ(buy.trades.size(), 1UL);

  EXPECT_FALSE(f.engine.cancel(56001, f.alice, latency.cancel_arrival(2)));
  EXPECT_TRUE(f.engine.open_orders().empty());
  EXPECT_TRUE(f.engine.topN(lob::Side::Ask, 10).empty());
  EXPECT_EQ(count_events(f.events, "order.canceled"), 0);
  EXPECT_EQ(count_events(f.events, "trade"), 1);
  EXPECT_EQ(f.ledger.balance(f.alice, f.base_asset).total, alice_base_before.total - 1);
  EXPECT_EQ(f.ledger.balance(f.alice, f.base_asset).locked, 0);
  EXPECT_EQ(f.ledger.balance(f.alice, f.quote_asset).total, alice_quote_before.total + 100);
  EXPECT_EQ(f.ledger.balance(f.bob, f.base_asset).total, bob_base_before.total + 1);
  EXPECT_EQ(f.ledger.balance(f.bob, f.quote_asset).total, bob_quote_before.total - 100);
  EXPECT_EQ(f.ledger.balance(f.bob, f.quote_asset).locked, 0);
}

TEST(SimulationClockRegression, CancelLatencyBeforeTakerPreventsExecution) {
  SpotEngineFixture f;
  TestLatencyModel latency{/*order_latency=*/1, /*cancel_latency=*/1, /*market_data_latency=*/1};
  const auto alice_base_before = f.ledger.balance(f.alice, f.base_asset);
  const auto bob_quote_before = f.ledger.balance(f.bob, f.quote_asset);

  EXPECT_TRUE(f.submit(f.alice, 56101, lob::Side::Ask, 100, 1, lob::POST_ONLY, latency.order_arrival(1)).accepted);
  EXPECT_TRUE(f.engine.cancel(56101, f.alice, latency.cancel_arrival(2)));
  auto buy = f.submit(f.bob, 56102, lob::Side::Bid, 100, 1, lob::IOC, latency.order_arrival(4));

  EXPECT_TRUE_MSG(buy.accepted, buy.reason);
  EXPECT_TRUE(buy.trades.empty());
  EXPECT_TRUE(f.engine.open_orders().empty());
  EXPECT_TRUE(f.engine.topN(lob::Side::Ask, 10).empty());
  EXPECT_EQ(count_events(f.events, "trade"), 0);
  EXPECT_EQ(count_events(f.events, "order.canceled"), 1);
  EXPECT_EQ(f.ledger.balance(f.alice, f.base_asset).total, alice_base_before.total);
  EXPECT_EQ(f.ledger.balance(f.alice, f.base_asset).locked, 0);
  EXPECT_EQ(f.ledger.balance(f.bob, f.quote_asset).total, bob_quote_before.total);
  EXPECT_EQ(f.ledger.balance(f.bob, f.quote_asset).locked, 0);
}

TEST(SimulationClockRegression, SameDecisionTimeDifferentLatencyChangesQueuePosition) {
  SpotEngineFixture f;
  TestLatencyModel fast{/*order_latency=*/1, /*cancel_latency=*/1, /*market_data_latency=*/1};
  TestLatencyModel slow{/*order_latency=*/5, /*cancel_latency=*/1, /*market_data_latency=*/1};
  const lob::Timestamp decision_ts = 100;

  EXPECT_TRUE(f.submit(f.alice, 56201, lob::Side::Ask, 100, 1, lob::POST_ONLY,
                       fast.order_arrival(decision_ts)).accepted);
  EXPECT_TRUE(f.submit(f.carol, 56202, lob::Side::Ask, 100, 1, lob::POST_ONLY,
                       slow.order_arrival(decision_ts)).accepted);
  auto buy = f.submit(f.bob, 56203, lob::Side::Bid, 100, 1, lob::IOC, 200);

  EXPECT_TRUE_MSG(buy.accepted, buy.reason);
  EXPECT_EQ(buy.trades.size(), 1UL);
  EXPECT_EQ(buy.trades.front().seller, f.alice);
  EXPECT_FALSE(has_open_order(f, 56201));
  EXPECT_TRUE(has_open_order(f, 56202));
}

TEST(SimulationClockRegression, SameDecisionTimeLatencyQueueProcessesByArrivalNotConstructionOrder) {
  SpotEngineFixture f;
  TestLatencyModel fast{/*order_latency=*/1, /*cancel_latency=*/1, /*market_data_latency=*/1};
  TestLatencyModel slow{/*order_latency=*/5, /*cancel_latency=*/1, /*market_data_latency=*/1};
  TestEventQueue queue;
  const lob::Timestamp decision_ts = 100;

  queue.push_order(TestOrderAction{slow.order_arrival(decision_ts), "submit", f.carol, 56402,
                                   lob::Side::Ask, 100, 1, lob::POST_ONLY});
  queue.push_order(TestOrderAction{fast.order_arrival(decision_ts), "submit", f.alice, 56401,
                                   lob::Side::Ask, 100, 1, lob::POST_ONLY});

  while (!queue.empty()) {
    const auto action = queue.pop_order();
    EXPECT_TRUE_MSG(f.submit(action.user, action.order_id, action.side, action.price, action.qty,
                             action.flags, action.arrival_ts).accepted,
                    "order_id=" + std::to_string(action.order_id));
  }

  auto buy = f.submit(f.bob, 56403, lob::Side::Bid, 100, 1, lob::IOC, 200);
  EXPECT_TRUE_MSG(buy.accepted, buy.reason);
  EXPECT_EQ(buy.trades.size(), 1UL);
  EXPECT_EQ(buy.trades.front().seller, f.alice);
  EXPECT_FALSE(has_open_order(f, 56401));
  EXPECT_TRUE(has_open_order(f, 56402));
}

TEST(SimulationClockRegression, MarketDataLatencyDelaysObservation) {
  TestLatencyModel latency{/*order_latency=*/1, /*cancel_latency=*/1, /*market_data_latency=*/5};

  EXPECT_EQ(latency.market_data_arrival(100), 105);
}

TEST(SimulationClockRegression, BotCannotObserveFutureTrades) {
  TestLatencyModel latency{/*order_latency=*/1, /*cancel_latency=*/1, /*market_data_latency=*/5};
  const lob::Timestamp exchange_trade_ts = 100;
  const lob::Timestamp bot_decision_ts = 103;

  EXPECT_TRUE(bot_decision_ts < latency.market_data_arrival(exchange_trade_ts));
}

TEST(SimulationClockRegression, BotCannotObserveTradeBeforeMarketDataArrival) {
  SpotEngineFixture f;
  TestLatencyModel latency{/*order_latency=*/1, /*cancel_latency=*/1, /*market_data_latency=*/5};
  TestMarketDataFeed feed(latency);
  feed.publish_trade(committed_spot_trade_at_100(f));

  EXPECT_TRUE(feed.visible_trades(103).empty());
}

TEST(SimulationClockRegression, BotCanObserveTradeAfterMarketDataArrival) {
  SpotEngineFixture f;
  TestLatencyModel latency{/*order_latency=*/1, /*cancel_latency=*/1, /*market_data_latency=*/5};
  TestMarketDataFeed feed(latency);
  feed.publish_trade(committed_spot_trade_at_100(f));

  EXPECT_EQ(feed.visible_trades(105).size(), 1UL);
}

TEST(SimulationClockRegression, MarketDataVisibleExactlyAtArrivalTimestamp) {
  SpotEngineFixture f;
  TestLatencyModel latency{/*order_latency=*/1, /*cancel_latency=*/1, /*market_data_latency=*/5};
  TestMarketDataFeed feed(latency);
  feed.publish_trade(committed_spot_trade_at_100(f));

  EXPECT_TRUE(feed.visible_trades(104).empty());
  EXPECT_EQ(feed.visible_trades(105).size(), 1UL);
}

TEST(SimulationClockRegression, PublicTradeFeedRedactsBuyerSellerAndOrderIds) {
  SpotEngineFixture f;
  TestLatencyModel latency{/*order_latency=*/1, /*cancel_latency=*/1, /*market_data_latency=*/5};
  TestMarketDataFeed feed(latency);
  const auto trade = committed_spot_trade_at_100(f);
  feed.publish_trade(trade);

  static_assert(std::is_same_v<decltype(feed.visible_trades(105)), std::vector<PublicTrade>>);
  const auto visible = feed.visible_trades(105);
  EXPECT_EQ(visible.size(), 1UL);
  EXPECT_EQ(visible.front().market_id, trade.market_id);
  EXPECT_EQ(visible.front().ts, trade.ts);
  EXPECT_EQ(visible.front().price, 100);
  EXPECT_EQ(visible.front().qty, 1);
  EXPECT_EQ(visible.front().liquidity_side, trade.liquidity_side);
  EXPECT_EQ(visible.front().public_trade_id, 1ULL);
}

TEST(SimulationClockRegression, PrivateFillLatencyDelaysInventoryObservation) {
  SpotEngineFixture f;
  TestPrivateFeed private_feed(/*private_latency=*/3);
  const auto trade = committed_spot_trade_at_100(f);
  private_feed.publish_fill(f.bob, trade);

  EXPECT_TRUE(private_feed.visible_fills(f.bob, 102).empty());
  EXPECT_EQ(private_feed.visible_fills(f.bob, 103).size(), 1UL);
  EXPECT_TRUE(private_feed.visible_fills(f.carol, 200).empty());
}

TEST(SimulationClockRegression, PrivateFillVisibleExactlyAtArrivalTimestamp) {
  SpotEngineFixture f;
  TestPrivateFeed private_feed(/*private_latency=*/3);
  const auto trade = committed_spot_trade_at_100(f);
  private_feed.publish_fill(f.bob, trade);

  EXPECT_TRUE(private_feed.visible_fills(f.bob, 102).empty());
  EXPECT_EQ(private_feed.visible_fills(f.bob, 103).size(), 1UL);
  EXPECT_TRUE(private_feed.visible_fills(f.carol, 103).empty());
}

TEST(SimulationClockRegression, PrivateFillVisibleOnlyToOwner) {
  SpotEngineFixture f;
  TestPrivateFeed private_feed(/*private_latency=*/0);
  const auto trade = committed_spot_trade_at_100(f);
  private_feed.publish_fill(f.alice, trade);
  private_feed.publish_fill(f.bob, trade);

  EXPECT_EQ(private_feed.visible_fills(f.alice, 100).size(), 1UL);
  EXPECT_EQ(private_feed.visible_fills(f.bob, 100).size(), 1UL);
  EXPECT_TRUE(private_feed.visible_fills(f.carol, 100).empty());
}

TEST(SimulationClockRegression, PrivateFeedDoesNotExposeCounterpartyOrderIdToNonOwner) {
  SpotEngineFixture f;
  TestPrivateFeed private_feed(/*private_latency=*/0);
  const auto trade = committed_spot_trade_at_100(f);
  private_feed.publish_fill(f.alice, trade);
  private_feed.publish_fill(f.bob, trade);

  EXPECT_TRUE(private_feed.visible_fills(f.carol, 200).empty());
  EXPECT_EQ(private_feed.visible_fills(f.alice, 200).front().seller_order_id, trade.seller_order_id);
  EXPECT_EQ(private_feed.visible_fills(f.bob, 200).front().buyer_order_id, trade.buyer_order_id);
}

TEST(SimulationClockRegression, ReplayWithSameSeedIsDeterministic) {
  EXPECT_TRUE(deterministic_seed_trace(42, 20) == deterministic_seed_trace(42, 20));
}

TEST(SimulationClockRegression, ReplayWithDifferentSeedCanDiverge) {
  EXPECT_TRUE(deterministic_seed_trace(42, 20) != deterministic_seed_trace(43, 20));
}

TEST(SimulationClockRegression, ReplaySameSeedProducesSameFinalExchangeState) {
  const auto a = run_seeded_spot_simulation(42, 50);
  const auto b = run_seeded_spot_simulation(42, 50);

  EXPECT_TRUE(a.action_trace == b.action_trace);
  EXPECT_TRUE(same_replay_state(a, b));
}

TEST(SimulationClockRegression, ReplayDifferentSeedDivergesActionTrace) {
  const auto a = run_seeded_spot_simulation(42, 50);
  const auto b = run_seeded_spot_simulation(43, 50);

  EXPECT_TRUE(a.action_trace != b.action_trace);
}

TEST(SimulationClockRegression, BotHarnessExposesOnlyPublicAndOwnPrivateData) {
  SpotEngineFixture f;
  TestLatencyModel latency{/*order_latency=*/1, /*cancel_latency=*/1, /*market_data_latency=*/5};
  TestMarketDataFeed public_feed(latency);
  TestPrivateFeed private_feed(/*private_latency=*/3);
  const auto trade = committed_spot_trade_at_100(f);
  public_feed.publish_trade(trade);
  private_feed.publish_fill(f.alice, trade);
  private_feed.publish_fill(f.bob, trade);

  const auto carol_context = make_bot_context(f.carol, 105, public_feed, private_feed, f.ledger, f.quote_asset);
  const auto bob_context = make_bot_context(f.bob, 103, public_feed, private_feed, f.ledger, f.quote_asset);

  EXPECT_EQ(carol_context.public_trades.size(), 1UL);
  EXPECT_EQ(carol_context.public_trades.front().price, 100);
  EXPECT_EQ(carol_context.public_trades.front().qty, 1);
  EXPECT_TRUE(carol_context.own_fills.empty());
  EXPECT_EQ(carol_context.own_balance.user, f.carol);
  EXPECT_EQ(carol_context.own_balance.asset, f.quote_asset);
  EXPECT_EQ(bob_context.own_fills.size(), 1UL);
}

TEST(SimulationClockRegression, BotContextDoesNotExposeOtherUserBalances) {
  SpotEngineFixture f;
  TestMarketDataFeed public_feed(TestLatencyModel{1, 1, 5});
  TestPrivateFeed private_feed(/*private_latency=*/0);
  const auto trade = committed_spot_trade_at_100(f);
  public_feed.publish_trade(trade);
  private_feed.publish_fill(f.alice, trade);
  private_feed.publish_fill(f.bob, trade);

  const auto carol_context = make_bot_context(f.carol, 105, public_feed, private_feed, f.ledger, f.quote_asset);

  EXPECT_EQ(carol_context.own_balance.user, f.carol);
  EXPECT_NE(carol_context.own_balance.user, f.alice);
  EXPECT_NE(carol_context.own_balance.user, f.bob);
  EXPECT_TRUE(carol_context.own_fills.empty());
}

TEST(SimulationClockRegression, BotContextUsesRedactedPublicTrades) {
  SpotEngineFixture f;
  TestMarketDataFeed public_feed(TestLatencyModel{1, 1, 5});
  TestPrivateFeed private_feed(/*private_latency=*/0);
  const auto trade = committed_spot_trade_at_100(f);
  public_feed.publish_trade(trade);

  const auto ctx = make_bot_context(f.carol, 105, public_feed, private_feed, f.ledger, f.quote_asset);

  static_assert(std::is_same_v<decltype(ctx.public_trades), std::vector<PublicTrade>>);
  EXPECT_EQ(ctx.public_trades.size(), 1UL);
  EXPECT_EQ(ctx.public_trades.front().price, 100);
  EXPECT_EQ(ctx.public_trades.front().qty, 1);
  EXPECT_EQ(ctx.public_trades.front().public_trade_id, 1ULL);
}

TEST(SimulationClockRegression, BotContextPrivateFillsAreOwnerOnly) {
  SpotEngineFixture f;
  TestMarketDataFeed public_feed(TestLatencyModel{1, 1, 5});
  TestPrivateFeed private_feed(/*private_latency=*/0);
  const auto trade = committed_spot_trade_at_100(f);
  public_feed.publish_trade(trade);
  private_feed.publish_fill(f.alice, trade);
  private_feed.publish_fill(f.bob, trade);

  const auto alice_context = make_bot_context(f.alice, 105, public_feed, private_feed, f.ledger, f.quote_asset);
  const auto bob_context = make_bot_context(f.bob, 105, public_feed, private_feed, f.ledger, f.quote_asset);
  const auto carol_context = make_bot_context(f.carol, 105, public_feed, private_feed, f.ledger, f.quote_asset);

  EXPECT_EQ(alice_context.own_fills.size(), 1UL);
  EXPECT_EQ(bob_context.own_fills.size(), 1UL);
  EXPECT_TRUE(carol_context.own_fills.empty());
}
