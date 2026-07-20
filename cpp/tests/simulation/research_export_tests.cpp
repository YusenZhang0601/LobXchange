#include "lobx/simulation/research_runner.hpp"

#include "test_helpers/test_framework.hpp"

#include <string>
#include <utility>
#include <vector>

using namespace lobx::sim;

namespace {

RankedStrategyResult ranked_row(int rank, std::string name, lobx::UserId user, long double score) {
  StrategyMetrics metrics{};
  metrics.user = user;
  metrics.bot_name = name;
  metrics.net_pnl = score;
  metrics.gross_pnl = score + 1.0L;
  metrics.fees_paid = 1;
  metrics.fills = 2;
  metrics.accepted_orders = 3;
  metrics.rejected_orders = 4;
  return RankedStrategyResult{rank, std::move(name), user, {{"bid_px", 99}}, score, metrics};
}

ResearchRunResult small_run() {
  ResearchRunner runner;
  ScenarioConfig config{42,
                        10,
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
                                   {{"side", 0}, {"target_qty", 1}, {"limit_price", 101}, {"max_avg_price", 101}}}}};
  return runner.run_scenario(config);
}

} // namespace

TEST(ResearchExportTests, RankedResultsCsvHasStableHeader) {
  const std::string csv = export_ranked_results_csv({ranked_row(1, "mm", 10, 7)});

  const std::string expected = "rank,bot_name,user,score,net_pnl,gross_pnl,fees_paid,fills,accepted_orders,rejected_orders\n";
  EXPECT_TRUE(csv.rfind(expected, 0) == 0);
}

TEST(ResearchExportTests, RankedResultsCsvIsDeterministic) {
  const std::vector<RankedStrategyResult> rows{ranked_row(1, "mm", 10, 7), ranked_row(2, "taker", 20, 3)};

  EXPECT_EQ(export_ranked_results_csv(rows), export_ranked_results_csv(rows));
}

TEST(ResearchExportTests, AggregatedStatsCsvHasStableHeader) {
  AggregatedStrategyStats stats{};
  stats.bot_name = "mm";
  stats.user = 10;
  stats.runs = 3;

  const std::string csv = export_aggregated_stats_csv({stats});

  const std::string expected = "bot_name,user,runs,mean_net_pnl,median_net_pnl,min_net_pnl,max_net_pnl,win_rate\n";
  EXPECT_TRUE(csv.rfind(expected, 0) == 0);
}

TEST(ResearchExportTests, RunSummaryJsonIsDeterministic) {
  const ResearchRunResult result = small_run();

  EXPECT_EQ(export_run_summary_json(result), export_run_summary_json(result));
}

TEST(ResearchExportTests, RunSummaryJsonContainsInvariants) {
  const std::string json = export_run_summary_json(small_run());

  EXPECT_TRUE(json.find("\"invariants\"") != std::string::npos);
  EXPECT_TRUE(json.find("\"ledger\"") != std::string::npos);
  EXPECT_TRUE(json.find("\"book_open\"") != std::string::npos);
}

TEST(ResearchExportTests, RunSummaryJsonContainsMetrics) {
  const std::string json = export_run_summary_json(small_run());

  EXPECT_TRUE(json.find("\"metrics\"") != std::string::npos);
  EXPECT_TRUE(json.find("\"bot_name\":\"mm\"") != std::string::npos);
}
