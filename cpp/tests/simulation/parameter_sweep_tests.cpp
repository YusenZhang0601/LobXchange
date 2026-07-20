#include "test_helpers/multi_seed_evaluation.hpp"
#include "test_helpers/parameter_sweep.hpp"
#include "test_helpers/strategy_ranking.hpp"
#include "test_helpers/test_framework.hpp"

#include <cmath>
#include <string>
#include <vector>

using namespace lobx_test;

namespace {

ScenarioConfig sweep_base() {
  return ScenarioConfig{42,
                        25,
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

double param_value(const ScenarioConfig& config, const std::string& bot_name, const std::string& param_name) {
  const BotConfig* bot = find_bot_config(config, bot_name);
  if (bot == nullptr) return -1.0;
  const auto it = bot->params.find(param_name);
  return it == bot->params.end() ? -1.0 : it->second;
}

} // namespace

TEST(ParameterSweepTests, ParameterSweepEnumeratesAllCombinations) {
  const auto expanded = expand_parameter_sweep(
      sweep_base(),
      {SweepParam{"mm", "bid_px", {98, 99}},
       SweepParam{"mm", "ask_px", {101, 102}}});

  EXPECT_EQ(expanded.size(), 4UL);
  EXPECT_EQ(param_value(expanded[0], "mm", "bid_px"), 98.0);
  EXPECT_EQ(param_value(expanded[0], "mm", "ask_px"), 101.0);
  EXPECT_EQ(param_value(expanded[1], "mm", "bid_px"), 98.0);
  EXPECT_EQ(param_value(expanded[1], "mm", "ask_px"), 102.0);
  EXPECT_EQ(param_value(expanded[2], "mm", "bid_px"), 99.0);
  EXPECT_EQ(param_value(expanded[2], "mm", "ask_px"), 101.0);
  EXPECT_EQ(param_value(expanded[3], "mm", "bid_px"), 99.0);
  EXPECT_EQ(param_value(expanded[3], "mm", "ask_px"), 102.0);
}

TEST(ParameterSweepTests, ParameterSweepOrderIsDeterministic) {
  const ScenarioConfig base = sweep_base();
  const std::vector<SweepParam> params{
      SweepParam{"mm", "bid_px", {98, 99}},
      SweepParam{"mm", "ask_px", {101, 102}}};

  const auto a = expand_parameter_sweep(base, params);
  const auto b = expand_parameter_sweep(base, params);

  EXPECT_EQ(a.size(), b.size());
  for (size_t i = 0; i < a.size(); ++i) {
    EXPECT_EQ(param_value(a[i], "mm", "bid_px"), param_value(b[i], "mm", "bid_px"));
    EXPECT_EQ(param_value(a[i], "mm", "ask_px"), param_value(b[i], "mm", "ask_px"));
  }
}

TEST(ParameterSweepTests, ParameterSweepDoesNotMutateBaseConfig) {
  ScenarioConfig base = sweep_base();
  const double bid_before = param_value(base, "mm", "bid_px");
  const double ask_before = param_value(base, "mm", "ask_px");

  (void)expand_parameter_sweep(base, {SweepParam{"mm", "bid_px", {98, 99}}});

  EXPECT_EQ(param_value(base, "mm", "bid_px"), bid_before);
  EXPECT_EQ(param_value(base, "mm", "ask_px"), ask_before);
}

TEST(ParameterSweepTests, ParameterSweepSameSeedDeterministic) {
  const auto a = run_parameter_sweep(sweep_base(), {SweepParam{"mm", "bid_px", {98, 99}}});
  const auto b = run_parameter_sweep(sweep_base(), {SweepParam{"mm", "bid_px", {98, 99}}});

  EXPECT_EQ(a.size(), b.size());
  for (size_t i = 0; i < a.size(); ++i) {
    EXPECT_TRUE(same_bot_run_result(a[i].result, b[i].result));
  }
}

TEST(ParameterSweepTests, ParameterSweepRecordsMetricsForEachRun) {
  const auto runs = run_parameter_sweep(sweep_base(), {SweepParam{"mm", "bid_px", {98, 99}}});

  EXPECT_EQ(runs.size(), 2UL);
  for (const SweepRun& run : runs) {
    EXPECT_TRUE(run.result.metrics.find(10) != run.result.metrics.end());
    EXPECT_TRUE(run.result.metrics.find(20) != run.result.metrics.end());
  }
}

TEST(ParameterSweepTests, ParameterSweepCanTargetSpecificBotByName) {
  ScenarioConfig base = sweep_base();
  base.bots.push_back(BotConfig{50,
                                "mm2",
                                "market_maker",
                                TestLatencyModel{0, 0, 1},
                                {{"bid_px", 97}, {"ask_px", 103}, {"qty", 1}}});

  const auto expanded = expand_parameter_sweep(base, {SweepParam{"mm2", "bid_px", {96, 97}}});

  EXPECT_EQ(expanded.size(), 2UL);
  EXPECT_EQ(param_value(expanded[0], "mm", "bid_px"), 99.0);
  EXPECT_EQ(param_value(expanded[0], "mm2", "bid_px"), 96.0);
  EXPECT_EQ(param_value(expanded[1], "mm2", "bid_px"), 97.0);
}

TEST(ParameterSweepTests, ParameterSweepRejectsUnknownBotName) {
  const auto expanded = expand_parameter_sweep(sweep_base(), {SweepParam{"missing", "bid_px", {98}}});

  EXPECT_TRUE(expanded.empty());
}

TEST(ParameterSweepTests, ParameterSweepRejectsUnknownParamName) {
  const auto expanded = expand_parameter_sweep(sweep_base(), {SweepParam{"mm", "telepathy", {1}}});

  EXPECT_TRUE(expanded.empty());
}

TEST(StrategyResearchFlow, SweepRankAndMultiSeedEvaluationWorkTogether) {
  const ScenarioConfig base = sweep_base();
  const auto runs = run_parameter_sweep(
      base,
      {SweepParam{"mm", "bid_px", {98, 99}},
       SweepParam{"mm", "ask_px", {101, 102}}});
  const auto ranking = rank_sweep_results(runs, "mm", RankingMetric::NetPnl);

  EXPECT_EQ(runs.size(), 4UL);
  EXPECT_EQ(ranking.size(), 4UL);
  if (ranking.empty()) return;
  EXPECT_EQ(ranking.front().rank, 1);

  ScenarioConfig top = runs[static_cast<size_t>(ranking.front().rank - 1)].config;
  const auto seed_runs = run_multi_seed_evaluation(top, {1, 2, 3});
  const auto stats = aggregate_strategy_stats(seed_runs, "mm");

  EXPECT_EQ(seed_runs.size(), 3UL);
  EXPECT_EQ(stats.runs, 3);
  EXPECT_TRUE(std::isfinite(static_cast<double>(stats.mean_net_pnl)));
  EXPECT_TRUE(std::isfinite(static_cast<double>(stats.min_net_pnl)));
  EXPECT_TRUE(std::isfinite(static_cast<double>(stats.max_net_pnl)));
}
