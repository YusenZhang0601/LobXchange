#include "lobx/simulation/research_runner.hpp"

#include "test_helpers/test_framework.hpp"

#include <map>
#include <string>
#include <vector>

using namespace lobx::sim;

namespace {

ScenarioConfig base_research_scenario(uint64_t seed = 42) {
  return ScenarioConfig{seed,
                        30,
                        "BTC-USDT",
                        {BotConfig{10,
                                   "mm",
                                   "market_maker",
                                   LatencyConfig{0, 0, 1, 1},
                                   {{"bid_px", 99}, {"ask_px", 101}, {"qty", 1}}},
                         BotConfig{20,
                                   "taker",
                                   "taker_sweep",
                                   LatencyConfig{0, 0, 1, 1},
                                   {{"side", 0}, {"target_qty", 1}, {"limit_price", 101}, {"max_avg_price", 101}}},
                         BotConfig{40,
                                   "noise",
                                   "noise_trader",
                                   LatencyConfig{1, 0, 1, 1},
                                   {}}}};
}

SweepRun fake_sweep_run(std::string bot_name, lobx::UserId user, long double net_pnl, std::map<std::string, double> params = {}) {
  StrategyMetrics metrics{};
  metrics.user = user;
  metrics.bot_name = bot_name;
  metrics.net_pnl = net_pnl;
  metrics.gross_pnl = net_pnl + 1.0L;
  metrics.fees_paid = 1;
  ResearchRunResult result{};
  result.metrics[user] = metrics;
  return SweepRun{ScenarioConfig{1,
                                 1,
                                 "BTC-USDT",
                                 {BotConfig{user, std::move(bot_name), "market_maker", LatencyConfig{0, 0, 0, 0}, std::move(params)}}},
                  result};
}

SeedEvaluationRun fake_seed_run(uint64_t seed, long double net_pnl) {
  StrategyMetrics metrics{};
  metrics.user = 10;
  metrics.bot_name = "mm";
  metrics.net_pnl = net_pnl;
  ResearchRunResult result{};
  result.config = ScenarioConfig{seed,
                                 1,
                                 "BTC-USDT",
                                 {BotConfig{10, "mm", "market_maker", LatencyConfig{0, 0, 0, 0}, {}}}};
  result.metrics[10] = metrics;
  return SeedEvaluationRun{seed, result};
}

} // namespace

TEST(ResearchRunnerTests, RunScenarioSameConfigDeterministic) {
  ResearchRunner runner;
  const ScenarioConfig config = base_research_scenario(77);

  const ResearchRunResult a = runner.run_scenario(config);
  const ResearchRunResult b = runner.run_scenario(config);

  EXPECT_TRUE(same_research_result(a, b));
}

TEST(ResearchRunnerTests, RunScenarioRejectsInvalidConfig) {
  ResearchRunner runner;
  ScenarioConfig config = base_research_scenario();
  config.ticks = 0;

  const ValidationResult validation = validate_scenario_config(config);
  const ResearchRunResult result = runner.run_scenario(config);

  EXPECT_FALSE(validation.ok);
  EXPECT_FALSE(result.ledger_invariant_ok);
  EXPECT_TRUE(!result.action_trace.empty());
}

TEST(ResearchRunnerTests, RunScenarioRecordsMetricsForEachBot) {
  ResearchRunner runner;
  const ResearchRunResult result = runner.run_scenario(base_research_scenario());

  EXPECT_TRUE(result.metrics.find(10) != result.metrics.end());
  EXPECT_TRUE(result.metrics.find(20) != result.metrics.end());
  EXPECT_TRUE(result.metrics.find(40) != result.metrics.end());
  EXPECT_EQ(result.metrics.at(10).bot_name, std::string("mm"));
}

TEST(ResearchRunnerTests, RunScenarioMaintainsInvariants) {
  ResearchRunner runner;
  const ResearchRunResult result = runner.run_scenario(base_research_scenario());

  EXPECT_TRUE(result.ledger_invariant_ok);
  EXPECT_TRUE(result.book_open_consistency_ok);
}

TEST(ResearchRunnerTests, RunScenarioDoesNotLeakPrivateData) {
  ResearchRunner runner;
  const ResearchRunResult result = runner.run_scenario(base_research_scenario());

  EXPECT_TRUE(result.no_private_data_leak);
  EXPECT_TRUE(result.no_future_public_data_leak);
}

TEST(ResearchRunnerTests, ParameterSweepEnumeratesAllCombinations) {
  const auto expanded = expand_parameter_sweep(
      base_research_scenario(),
      {SweepParam{"mm", "bid_px", {98, 99}},
       SweepParam{"mm", "ask_px", {101, 102}}});

  EXPECT_EQ(expanded.size(), 4UL);
  EXPECT_EQ(expanded[0].bots[0].params.at("bid_px"), 98.0);
  EXPECT_EQ(expanded[0].bots[0].params.at("ask_px"), 101.0);
  EXPECT_EQ(expanded[3].bots[0].params.at("bid_px"), 99.0);
  EXPECT_EQ(expanded[3].bots[0].params.at("ask_px"), 102.0);
}

TEST(ResearchRunnerTests, ParameterSweepDoesNotMutateBaseConfig) {
  ScenarioConfig base = base_research_scenario();
  const double bid_before = base.bots[0].params.at("bid_px");

  (void)expand_parameter_sweep(base, {SweepParam{"mm", "bid_px", {98, 99}}});

  EXPECT_EQ(base.bots[0].params.at("bid_px"), bid_before);
}

TEST(ResearchRunnerTests, ParameterSweepOrderDeterministic) {
  const ScenarioConfig base = base_research_scenario();
  const std::vector<SweepParam> params{SweepParam{"mm", "bid_px", {98, 99}},
                                       SweepParam{"mm", "ask_px", {101, 102}}};

  const auto a = expand_parameter_sweep(base, params);
  const auto b = expand_parameter_sweep(base, params);

  EXPECT_EQ(a.size(), b.size());
  for (size_t i = 0; i < a.size(); ++i) {
    EXPECT_EQ(a[i].bots[0].params.at("bid_px"), b[i].bots[0].params.at("bid_px"));
    EXPECT_EQ(a[i].bots[0].params.at("ask_px"), b[i].bots[0].params.at("ask_px"));
  }
}

TEST(ResearchRunnerTests, RankingByNetPnlOrdersDescending) {
  const std::vector<SweepRun> runs{
      fake_sweep_run("mm", 10, 10),
      fake_sweep_run("mm", 10, 5),
      fake_sweep_run("mm", 10, -1)};

  const auto ranked = rank_sweep_results(runs, "mm", RankingMetric::NetPnl);

  EXPECT_EQ(ranked.size(), 3UL);
  EXPECT_EQ(static_cast<int>(ranked[0].score), 10);
  EXPECT_EQ(static_cast<int>(ranked[2].score), -1);
}

TEST(ResearchRunnerTests, RankingTieBreaksDeterministically) {
  const std::vector<SweepRun> runs{
      fake_sweep_run("mm", 10, 5, {{"bid_px", 99}}),
      fake_sweep_run("mm", 10, 5, {{"bid_px", 98}})};

  const auto ranked = rank_sweep_results(runs, "mm", RankingMetric::NetPnl);

  EXPECT_EQ(ranked.size(), 2UL);
  EXPECT_EQ(ranked[0].params.at("bid_px"), 98.0);
}

TEST(ResearchRunnerTests, MultiSeedEvaluationRunsAllSeeds) {
  ResearchRunner runner;
  const auto runs = run_multi_seed_evaluation(runner, base_research_scenario(), {1, 2, 3});

  EXPECT_EQ(runs.size(), 3UL);
  EXPECT_EQ(runs[0].seed, 1ULL);
  EXPECT_EQ(runs[2].seed, 3ULL);
}

TEST(ResearchRunnerTests, MultiSeedAggregationComputesMeanMedianMinMaxWinRate) {
  const auto stats = aggregate_strategy_stats({fake_seed_run(1, 10), fake_seed_run(2, -3),
                                               fake_seed_run(3, 20), fake_seed_run(4, 0)},
                                              "mm");

  EXPECT_EQ(stats.runs, 4);
  EXPECT_EQ(static_cast<int>(stats.mean_net_pnl), 6);
  EXPECT_EQ(stats.median_net_pnl, 5.0L);
  EXPECT_EQ(static_cast<int>(stats.min_net_pnl), -3);
  EXPECT_EQ(static_cast<int>(stats.max_net_pnl), 20);
  EXPECT_EQ(static_cast<int>(stats.win_rate * 100.0L), 50);
}
