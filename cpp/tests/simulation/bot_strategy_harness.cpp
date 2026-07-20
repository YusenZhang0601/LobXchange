#include "test_helpers/bot_test_agents.hpp"
#include "test_helpers/market_microstructure_helpers.hpp"
#include "test_helpers/test_framework.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace lobx_test;

namespace {

lob::Quantity qty_at(const std::vector<std::pair<lob::Tick, lob::Quantity>>& depth, lob::Tick price) {
  for (const auto& [level_price, qty] : depth) {
    if (level_price == price) return qty;
  }
  return 0;
}

BotInstance bot(lobx::UserId user, std::string name, TestLatencyModel latency, std::unique_ptr<Strategy> strategy) {
  return BotInstance{user, std::move(name), latency, std::move(strategy)};
}

BotAction action(lobx::UserId user,
                 lobx::OrderId order_id,
                 lob::Side side,
                 lob::Tick price,
                 lob::Quantity qty,
                 uint32_t flags,
                 lob::Timestamp decision_ts,
                 BotActionType type = BotActionType::SubmitLimit) {
  BotAction out{user, order_id, side, price, qty, flags};
  out.type = type;
  out.decision_ts = decision_ts;
  return out;
}

} // namespace

TEST(BotStrategyHarness, MarketMakerBotPlacesTwoSidedQuotes) {
  BotSimulationRunner runner(1);
  const lobx::UserId maker = runner.fixture().alice;
  runner.add_bot(bot(maker, "maker", TestLatencyModel{0, 0, 0},
                     std::make_unique<MarketMakerStrategy>(99, 101, 1, 610001)));

  const auto result = runner.run(1);

  EXPECT_EQ(qty_at(result.bids, 99), 1);
  EXPECT_EQ(qty_at(result.asks, 101), 1);
  EXPECT_TRUE(result.trades.empty());
  EXPECT_EQ(result.metrics.at(maker).submitted_orders, 2);
  EXPECT_EQ(result.metrics.at(maker).accepted_orders, 2);
}

TEST(BotStrategyHarness, MarketMakerBotUsesPostOnlyAndDoesNotCross) {
  BotSimulationRunner runner(2);
  EXPECT_TRUE(runner.fixture().submit(runner.fixture().carol, 610101, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  runner.add_bot(bot(runner.fixture().alice, "maker", TestLatencyModel{0, 0, 0},
                     std::make_unique<MarketMakerStrategy>(100, 102, 1, 610102)));

  const auto result = runner.run(1);

  EXPECT_TRUE(result.trades.empty());
  EXPECT_EQ(qty_at(result.asks, 100), 1);
  EXPECT_EQ(qty_at(result.bids, 100), 0);
  EXPECT_EQ(qty_at(result.asks, 102), 1);
}

TEST(BotStrategyHarness, TakerBotUsesSimulateFillBeforeIOC) {
  BotSimulationRunner runner(3);
  EXPECT_TRUE(runner.fixture().submit(runner.fixture().alice, 610201, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  runner.add_bot(bot(runner.fixture().bob, "taker", TestLatencyModel{0, 0, 0},
                     std::make_unique<TakerSweepStrategy>(lob::Side::Bid, 1, 100, 100.0L, 610202)));

  const auto result = runner.run(1);

  EXPECT_EQ(result.trades.size(), 1UL);
  if (result.trades.empty()) return;
  EXPECT_EQ(result.trades.front().buyer, runner.fixture().bob);
  EXPECT_EQ(result.trades.front().seller, runner.fixture().alice);
  EXPECT_EQ(result.trades.front().price, 100);
  EXPECT_EQ(result.trades.front().qty, 1);
}

TEST(BotStrategyHarness, TakerBotDoesNotSweepWhenSlippageTooHigh) {
  BotSimulationRunner runner(4);
  EXPECT_TRUE(runner.fixture().submit(runner.fixture().alice, 610301, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(runner.fixture().submit(runner.fixture().carol, 610302, lob::Side::Ask, 120, 1, lob::POST_ONLY, 2).accepted);
  runner.add_bot(bot(runner.fixture().bob, "taker", TestLatencyModel{0, 0, 0},
                     std::make_unique<TakerSweepStrategy>(lob::Side::Bid, 2, 120, 105.0L, 610303)));

  const auto result = runner.run(1);

  EXPECT_TRUE(result.trades.empty());
  EXPECT_EQ(qty_at(result.asks, 100), 1);
  EXPECT_EQ(qty_at(result.asks, 120), 1);
}

TEST(BotStrategyHarness, NoiseTraderSameSeedDeterministic) {
  auto run = [](uint64_t seed) {
    BotSimulationRunner runner(seed);
    runner.add_bot(bot(runner.fixture().alice, "noise", TestLatencyModel{0, 0, 0},
                       std::make_unique<NoiseTraderStrategy>(seed, 610401)));
    return runner.run(50);
  };

  EXPECT_TRUE(same_bot_run_result(run(777), run(777)));
}

TEST(BotStrategyHarness, BotActionsProcessedByLatencyOrder) {
  BotSimulationRunner runner(5);
  const lobx::UserId fast_user = runner.fixture().alice;
  const lobx::UserId slow_user = runner.fixture().carol;
  const lobx::UserId taker_user = runner.fixture().bob;

  // Slow bot is added first, but the lower latency action must arrive first.
  runner.add_bot(bot(slow_user, "slow", TestLatencyModel{5, 0, 0},
                     std::make_unique<UserStrategyStub>(
                         std::vector<BotAction>{action(slow_user, 610502, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1)})));
  runner.add_bot(bot(fast_user, "fast", TestLatencyModel{1, 0, 0},
                     std::make_unique<UserStrategyStub>(
                         std::vector<BotAction>{action(fast_user, 610501, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1)})));
  runner.add_bot(bot(taker_user, "taker", TestLatencyModel{0, 0, 0},
                     std::make_unique<UserStrategyStub>(
                         std::vector<BotAction>{action(taker_user, 610503, lob::Side::Bid, 100, 1, lob::IOC, 10)})));

  const auto result = runner.run(20);

  EXPECT_EQ(result.trades.size(), 1UL);
  if (result.trades.empty()) return;
  EXPECT_EQ(result.trades.front().seller, fast_user);
  EXPECT_EQ(qty_at(result.asks, 100), 1);
}

TEST(BotStrategyHarness, BotCannotObserveFutureTradesThroughContext) {
  BotSimulationRunner runner(6);
  EXPECT_TRUE(runner.fixture().submit(runner.fixture().alice, 610601, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  const auto buy = runner.fixture().submit(runner.fixture().bob, 610602, lob::Side::Bid, 100, 1, lob::IOC, 100);
  EXPECT_EQ(buy.trades.size(), 1UL);
  if (buy.trades.empty()) return;
  runner.publish_trade_for_test(buy.trades.front());

  const TestLatencyModel delayed_public{0, 0, 5};
  const auto before = runner.context_for_user(runner.fixture().carol, 103, delayed_public);
  const auto at_arrival = runner.context_for_user(runner.fixture().carol, 105, delayed_public);

  EXPECT_TRUE(before.public_trades.empty());
  EXPECT_EQ(at_arrival.public_trades.size(), 1UL);
  if (at_arrival.public_trades.empty()) return;
  EXPECT_EQ(at_arrival.public_trades.front().price, 100);
  EXPECT_TRUE(at_arrival.own_fills.empty());
}

TEST(BotStrategyHarness, BotCannotObserveOtherUserBalancesThroughContext) {
  BotSimulationRunner runner(7);
  EXPECT_TRUE(runner.fixture().submit(runner.fixture().alice, 610701, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  const auto buy = runner.fixture().submit(runner.fixture().bob, 610702, lob::Side::Bid, 100, 1, lob::IOC, 2);
  EXPECT_EQ(buy.trades.size(), 1UL);
  if (buy.trades.empty()) return;
  runner.publish_trade_for_test(buy.trades.front());

  const auto carol_ctx = runner.context_for_user(runner.fixture().carol, 10, TestLatencyModel{0, 0, 0});

  EXPECT_EQ(carol_ctx.quote_balance.user, runner.fixture().carol);
  EXPECT_EQ(carol_ctx.base_balance.user, runner.fixture().carol);
  EXPECT_TRUE(carol_ctx.own_fills.empty());
  EXPECT_EQ(carol_ctx.public_trades.size(), 1UL);
  if (carol_ctx.public_trades.empty()) return;
  EXPECT_EQ(carol_ctx.public_trades.front().price, 100);
}
