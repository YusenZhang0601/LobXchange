#include "test_helpers/multi_seed_evaluation.hpp"
#include "test_helpers/test_framework.hpp"

#include <string>
#include <vector>

using namespace lobx_test;

namespace {

ScenarioConfig seed_scenario() {
  return ScenarioConfig{1,
                        30,
                        "BTC-USDT",
                        {BotConfig{10,
                                   "mm",
                                   "market_maker",
                                   TestLatencyModel{0, 0, 1},
                                   {{"bid_px", 99}, {"ask_px", 101}, {"qty", 1}}},
                         BotConfig{20,
                                   "taker",
                                   "taker_sweep",
                                   TestLatencyModel{0, 0, 1},
                                   {{"side", 0}, {"target_qty", 1}, {"limit_price", 101}, {"max_avg_price", 101}}},
                         BotConfig{40,
                                   "noise",
                                   "noise_trader",
                                   TestLatencyModel{1, 0, 1},
                                   {}}}};
}

SeedEvaluationRun fake_seed_run(uint64_t seed, long double net_pnl) {
  StrategyMetrics metrics{};
  metrics.user = 10;
  metrics.net_pnl = net_pnl;
  BotRunResult result{};
  result.metrics[10] = metrics;
  return SeedEvaluationRun{seed,
                           ScenarioConfig{seed,
                                          1,
                                          "BTC-USDT",
                                          {BotConfig{10, "mm", "market_maker", TestLatencyModel{0, 0, 0}, {}}}},
                           result};
}

} // namespace

TEST(MultiSeedStrategyEvaluation, MultiSeedEvaluationRunsAllSeeds) {
  const auto runs = run_multi_seed_evaluation(seed_scenario(), {1, 2, 3});

  EXPECT_EQ(runs.size(), 3UL);
}

TEST(MultiSeedStrategyEvaluation, MultiSeedEvaluationUsesProvidedSeeds) {
  const auto runs = run_multi_seed_evaluation(seed_scenario(), {7, 11, 13});

  EXPECT_EQ(runs.size(), 3UL);
  EXPECT_EQ(runs[0].seed, 7ULL);
  EXPECT_EQ(runs[1].seed, 11ULL);
  EXPECT_EQ(runs[2].seed, 13ULL);
}

TEST(MultiSeedStrategyEvaluation, MultiSeedEvaluationAggregatesMeanNetPnl) {
  const auto stats = aggregate_strategy_stats({fake_seed_run(1, 10), fake_seed_run(2, 20), fake_seed_run(3, -3)}, "mm");

  EXPECT_EQ(stats.runs, 3);
  EXPECT_EQ(static_cast<int>(stats.mean_net_pnl), 9);
}

TEST(MultiSeedStrategyEvaluation, MultiSeedEvaluationAggregatesMedianNetPnl) {
  const auto odd = aggregate_strategy_stats({fake_seed_run(1, 10), fake_seed_run(2, -3), fake_seed_run(3, 20)}, "mm");
  const auto even = aggregate_strategy_stats({fake_seed_run(1, 10), fake_seed_run(2, 20), fake_seed_run(3, -3), fake_seed_run(4, 5)}, "mm");

  EXPECT_EQ(static_cast<int>(odd.median_net_pnl), 10);
  EXPECT_EQ(even.median_net_pnl, 7.5L);
}

TEST(MultiSeedStrategyEvaluation, MultiSeedEvaluationAggregatesWorstCaseNetPnl) {
  const auto stats = aggregate_strategy_stats({fake_seed_run(1, 10), fake_seed_run(2, -3), fake_seed_run(3, 20)}, "mm");

  EXPECT_EQ(static_cast<int>(stats.min_net_pnl), -3);
  EXPECT_EQ(static_cast<int>(stats.max_net_pnl), 20);
}

TEST(MultiSeedStrategyEvaluation, MultiSeedEvaluationComputesWinRate) {
  const auto stats = aggregate_strategy_stats({fake_seed_run(1, 10), fake_seed_run(2, -3), fake_seed_run(3, 20), fake_seed_run(4, 0)}, "mm");

  EXPECT_EQ(static_cast<int>(stats.win_rate * 100.0L), 50);
}

TEST(MultiSeedStrategyEvaluation, MultiSeedEvaluationDeterministicForSameSeeds) {
  const auto a = run_multi_seed_evaluation(seed_scenario(), {1, 2, 3});
  const auto b = run_multi_seed_evaluation(seed_scenario(), {1, 2, 3});

  EXPECT_EQ(a.size(), b.size());
  for (size_t i = 0; i < a.size(); ++i) {
    EXPECT_EQ(a[i].seed, b[i].seed);
    EXPECT_TRUE(same_bot_run_result(a[i].result, b[i].result));
  }
}

TEST(MultiSeedStrategyEvaluation, MultiSeedEvaluationExposesSeedVariance) {
  const auto runs = run_multi_seed_evaluation(seed_scenario(), {1, 2});

  EXPECT_EQ(runs.size(), 2UL);
  if (runs.size() < 2) return;
  EXPECT_TRUE_MSG(runs[0].result.action_trace != runs[1].result.action_trace,
                  "noise strategy should expose seed variance in action trace");
}
