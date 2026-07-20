#include "lobx/simulation/research_runner.hpp"

#include "test_helpers/test_framework.hpp"

#include <cmath>
#include <map>
#include <string>
#include <vector>

using namespace lobx::sim;

namespace {

ScenarioConfig workflow_scenario() {
  return ScenarioConfig{42,
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

bool same_params(const std::map<std::string, double>& a, const std::map<std::string, double>& b) {
  return a == b;
}

} // namespace

TEST(ResearchWorkflowIntegration, SweepRankAndMultiSeedEvaluationWorkTogether) {
  ResearchRunner runner;
  const ScenarioConfig base = workflow_scenario();
  const std::vector<SweepParam> params{SweepParam{"mm", "bid_px", {98, 99}},
                                       SweepParam{"mm", "ask_px", {101, 102}}};

  const std::vector<SweepRun> sweep = run_parameter_sweep(runner, base, params);
  const std::vector<RankedStrategyResult> ranking = rank_sweep_results(sweep, "mm", RankingMetric::NetPnl);

  EXPECT_EQ(sweep.size(), 4UL);
  EXPECT_EQ(ranking.size(), 4UL);
  if (ranking.empty()) return;
  EXPECT_EQ(ranking.front().rank, 1);

  ScenarioConfig top_config = sweep.front().config;
  for (const SweepRun& run : sweep) {
    if (same_params(run.config.bots.front().params, ranking.front().params)) {
      top_config = run.config;
      break;
    }
  }

  const std::vector<SeedEvaluationRun> seeds = run_multi_seed_evaluation(runner, top_config, {1, 2, 3});
  const AggregatedStrategyStats stats = aggregate_strategy_stats(seeds, "mm");
  const std::string ranked_csv = export_ranked_results_csv(ranking);
  const std::string stats_csv = export_aggregated_stats_csv({stats});

  EXPECT_EQ(seeds.size(), 3UL);
  EXPECT_EQ(stats.runs, 3);
  EXPECT_TRUE(!ranked_csv.empty());
  EXPECT_TRUE(!stats_csv.empty());
  EXPECT_TRUE(std::isfinite(static_cast<double>(stats.mean_net_pnl)));
  EXPECT_TRUE(std::isfinite(static_cast<double>(stats.min_net_pnl)));
  EXPECT_TRUE(std::isfinite(static_cast<double>(stats.max_net_pnl)));

  for (const SeedEvaluationRun& run : seeds) {
    EXPECT_TRUE(run.result.ledger_invariant_ok);
    EXPECT_TRUE(run.result.book_open_consistency_ok);
    const std::string json = export_run_summary_json(run.result);
    EXPECT_TRUE(json.find("\"invariants\"") != std::string::npos);
  }
}
