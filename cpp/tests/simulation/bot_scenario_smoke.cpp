#include "test_helpers/bot_test_agents.hpp"
#include "test_helpers/invariant_checker.hpp"
#include "test_helpers/market_microstructure_helpers.hpp"

using namespace lobx_test;

namespace {

PublicMarketData public_feed(SpotEngineFixture& f) {
  return PublicMarketData{f.engine.topN(lob::Side::Bid, 10), f.engine.topN(lob::Side::Ask, 10), {}};
}

} // namespace

TEST(BotScenarioSmoke, MarketMakerBotPlacesTwoSidedQuotes) {
  SpotEngineFixture f;
  MarketMakerBot maker{f.alice, 57001, 100, 1};

  const auto actions = maker.quote();
  EXPECT_TRUE(submit_bot_action(f, actions[0], 1).accepted);
  EXPECT_TRUE(submit_bot_action(f, actions[1], 2).accepted);

  EXPECT_EQ(top_qty(f.engine, lob::Side::Bid, 99), 1);
  EXPECT_EQ(top_qty(f.engine, lob::Side::Ask, 101), 1);
}

TEST(BotScenarioSmoke, TakerBotSweepsThinBookAndPaysSpread) {
  SpotEngineFixture f;
  MarketMakerBot maker{f.alice, 57011, 100, 1};
  TakerBot taker{f.bob, 57021};
  const auto quote = maker.quote();
  EXPECT_TRUE(submit_bot_action(f, quote[1], 1).accepted);
  const auto quote_before = f.ledger.balance(f.bob, f.quote_asset).total;

  auto buy = submit_bot_action(f, taker.sweep_ask(101, 1), 2);

  EXPECT_TRUE(buy.accepted);
  EXPECT_EQ(buy.trades.front().price, 101);
  EXPECT_EQ(quote_before - f.ledger.balance(f.bob, f.quote_asset).total, 101);
}

TEST(BotScenarioSmoke, NoiseTraderRandomFlowDoesNotBreakInvariants) {
  SpotEngineFixture f;
  NoiseTraderBot noise_a{f.alice, 57031, 1};
  NoiseTraderBot noise_b{f.bob, 57131, 2};

  for (int i = 0; i < 50; ++i) {
    (void)submit_bot_action(f, (i % 2 == 0) ? noise_a.next() : noise_b.next(), 100 + i);
    EXPECT_TRUE(f.ledger.invariant_ok());
    expect_topN_matches_open_orders(f.engine);
  }
}

TEST(BotScenarioSmoke, AdversarialBotCannotUseSelfLiquidityWithSTP) {
  SpotEngineFixture f;
  AdversarialBot adversary{f.alice, 57201};
  EXPECT_TRUE(f.submit(f.alice, 57200, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);

  auto self_cross = submit_bot_action(f, adversary.self_cross_bid(100), 2);

  EXPECT_TRUE(self_cross.accepted);
  EXPECT_TRUE(self_cross.trades.empty());
  EXPECT_FALSE(has_open_order(f.engine, 57200));
}

TEST(BotScenarioSmoke, AdversarialBotCannotMutateStateWithFailedOrders) {
  SpotEngineFixture f;
  AdversarialBot adversary{f.alice, 57211};
  EXPECT_TRUE(f.submit(f.carol, 57210, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  const auto asks_before = f.engine.topN(lob::Side::Ask, 10);
  const auto alice_quote_before = f.ledger.balance(f.alice, f.quote_asset);

  auto rejected = submit_bot_action(f, adversary.invalid_bid(100), 2);

  EXPECT_FALSE(rejected.accepted);
  EXPECT_TRUE(f.engine.topN(lob::Side::Ask, 10) == asks_before);
  EXPECT_EQ(f.ledger.balance(f.alice, f.quote_asset).total, alice_quote_before.total);
  EXPECT_EQ(f.ledger.balance(f.alice, f.quote_asset).locked, alice_quote_before.locked);
}

TEST(BotScenarioSmoke, UserStrategyPnLReplaysExactlyWithSameSeed) {
  auto run = [](uint32_t seed) {
    SpotEngineFixture f;
    UserStrategyBot bot{f.alice, 57301, seed};
    EXPECT_TRUE(f.submit(f.bob, 57390, lob::Side::Ask, 98, 20, lob::POST_ONLY, 1).accepted);
    const auto before = f.ledger.balance(f.alice, f.quote_asset).total;
    int ts = 2;
    for (const auto& action : bot.deterministic_actions(10)) {
      (void)submit_bot_action(f, action, ts++);
    }
    return f.ledger.balance(f.alice, f.quote_asset).total - before;
  };

  EXPECT_EQ(run(123), run(123));
}

TEST(BotScenarioSmoke, BotCannotReadInternalOpenOrders) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 57401, lob::Side::Ask, 100, 3, lob::POST_ONLY, 1).accepted);

  const auto feed = public_feed(f);

  EXPECT_EQ(feed.asks.size(), 1UL);
  EXPECT_EQ(feed.asks.front().first, 100);
  EXPECT_EQ(feed.asks.front().second, 3);
}

TEST(BotScenarioSmoke, BotReceivesOnlyPublicMarketDataAndOwnPrivateEvents) {
  SpotEngineFixture f;
  MarketMakerBot maker{f.alice, 57501, 100, 1};
  TakerBot taker{f.bob, 57511};
  EXPECT_TRUE(submit_bot_action(f, maker.quote()[1], 1).accepted);

  auto own_private_result = submit_bot_action(f, taker.sweep_ask(101, 1), 2);
  auto public_data = public_feed(f);

  EXPECT_TRUE(own_private_result.accepted);
  EXPECT_TRUE(public_data.asks.empty());
  EXPECT_EQ(own_private_result.trades.size(), 1UL);
}
