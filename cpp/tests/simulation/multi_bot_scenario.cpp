#include "test_helpers/bot_test_agents.hpp"
#include "test_helpers/market_microstructure_helpers.hpp"
#include "test_helpers/test_framework.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace lobx_test;

namespace {

BotInstance bot(lobx::UserId user, std::string name, TestLatencyModel latency, std::unique_ptr<Strategy> strategy) {
  return BotInstance{user, std::move(name), latency, std::move(strategy)};
}

BotAction action(lobx::UserId user,
                 lobx::OrderId order_id,
                 lob::Side side,
                 lob::Tick price,
                 lob::Quantity qty,
                 uint32_t flags,
                 lob::Timestamp decision_ts) {
  BotAction out{user, order_id, side, price, qty, flags};
  out.decision_ts = decision_ts;
  return out;
}

BotRunResult run_multi_bot(uint64_t seed) {
  BotSimulationRunner runner(seed, 0, 10, 100);
  runner.add_bot(bot(runner.fixture().alice, "maker", TestLatencyModel{0, 0, 1},
                     std::make_unique<MarketMakerStrategy>(99, 101, 1, 630001)));
  runner.add_bot(bot(runner.fixture().bob, "taker", TestLatencyModel{0, 0, 1},
                     std::make_unique<TakerSweepStrategy>(lob::Side::Bid, 1, 101, 101.0L, 631001)));
  runner.add_bot(bot(40, "noise-1", TestLatencyModel{1, 0, 2},
                     std::make_unique<NoiseTraderStrategy>(seed + 1, 632001)));
  runner.add_bot(bot(50, "noise-2", TestLatencyModel{2, 0, 1},
                     std::make_unique<NoiseTraderStrategy>(seed + 2, 633001)));
  runner.add_bot(bot(60, "noise-3", TestLatencyModel{3, 0, 2},
                     std::make_unique<NoiseTraderStrategy>(seed + 3, 634001)));
  runner.add_bot(bot(runner.fixture().carol, "scripted-user", TestLatencyModel{0, 0, 1},
                     std::make_unique<UserStrategyStub>(
                         std::vector<BotAction>{
                             action(runner.fixture().carol, 635001, lob::Side::Ask, 99, 1, lob::IOC, 8),
                             action(runner.fixture().carol, 635002, lob::Side::Bid, 101, 1, lob::IOC, 20)})));
  return runner.run(100);
}

} // namespace

TEST(MultiBotScenario, MultiBotScenarioSameSeedDeterministic) {
  EXPECT_TRUE(same_bot_run_result(run_multi_bot(9001), run_multi_bot(9001)));
}

TEST(MultiBotScenario, MultiBotScenarioMaintainsLedgerInvariant) {
  const auto result = run_multi_bot(9002);
  EXPECT_TRUE(result.ledger_invariant_ok);
}

TEST(MultiBotScenario, MultiBotScenarioMaintainsBookOpenConsistency) {
  const auto result = run_multi_bot(9003);
  EXPECT_TRUE(result.book_open_consistency_ok);
}

TEST(MultiBotScenario, MultiBotScenarioNoBotGetsPrivateDataLeak) {
  BotSimulationRunner runner(9004);
  EXPECT_TRUE(runner.fixture().submit(runner.fixture().alice, 636001, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  const auto buy = runner.fixture().submit(runner.fixture().bob, 636002, lob::Side::Bid, 100, 1, lob::IOC, 2);
  EXPECT_EQ(buy.trades.size(), 1UL);
  if (buy.trades.empty()) return;
  runner.publish_trade_for_test(buy.trades.front());

  const auto alice_ctx = runner.context_for_user(runner.fixture().alice, 10, TestLatencyModel{0, 0, 0});
  const auto carol_ctx = runner.context_for_user(runner.fixture().carol, 10, TestLatencyModel{0, 0, 0});

  EXPECT_EQ(alice_ctx.own_fills.size(), 1UL);
  EXPECT_EQ(alice_ctx.own_fills.front().own_order_id, 636001);
  EXPECT_TRUE(carol_ctx.own_fills.empty());
  EXPECT_EQ(carol_ctx.public_trades.size(), 1UL);
  EXPECT_EQ(carol_ctx.public_trades.front().price, 100);
  EXPECT_EQ(carol_ctx.quote_balance.user, runner.fixture().carol);
  EXPECT_EQ(carol_ctx.base_balance.user, runner.fixture().carol);
}

TEST(MultiBotScenario, MultiBotScenarioProducesNonEmptyTrades) {
  const auto result = run_multi_bot(9005);
  EXPECT_TRUE_MSG(!result.trades.empty(), "deterministic scenario should produce committed trades");
}
