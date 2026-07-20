#include "lobx/simulation/file_export.hpp"

#include "test_helpers/test_framework.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace lobx::sim;

namespace {

std::filesystem::path temp_root() {
  const std::filesystem::path root = std::filesystem::temp_directory_path() / "lobx_exchange_file_export_tests";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  return root;
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

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

AggregatedStrategyStats stats_row() {
  AggregatedStrategyStats stats{};
  stats.bot_name = "mm";
  stats.user = 10;
  stats.runs = 3;
  stats.mean_net_pnl = 1.0L;
  stats.median_net_pnl = 2.0L;
  stats.min_net_pnl = -1.0L;
  stats.max_net_pnl = 4.0L;
  stats.win_rate = 0.5L;
  return stats;
}

ResearchRunResult run_summary() {
  ResearchRunner runner;
  return runner.run_scenario(ScenarioConfig{42,
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
                                                       {{"side", 0}, {"target_qty", 1}, {"limit_price", 101}, {"max_avg_price", 101}}}}});
}

} // namespace

TEST(FileExportTests, WriteTextFileCreatesFile) {
  const auto root = temp_root();
  const auto path = root / "plain.txt";

  const FileWriteResult result = write_text_file(path.string(), "hello");

  EXPECT_TRUE_MSG(result.ok, result.reason);
  EXPECT_EQ(read_file(path), std::string("hello"));
}

TEST(FileExportTests, WriteRankedResultsCsvCreatesDeterministicFile) {
  const auto root = temp_root();
  const auto path = root / "ranking.csv";
  const std::vector<RankedStrategyResult> ranked{ranked_row(1, "mm", 10, 7)};

  const FileWriteResult a = write_ranked_results_csv(path.string(), ranked);
  const std::string first = read_file(path);
  const FileWriteResult b = write_ranked_results_csv(path.string(), ranked);

  EXPECT_TRUE_MSG(a.ok, a.reason);
  EXPECT_TRUE_MSG(b.ok, b.reason);
  EXPECT_EQ(first, read_file(path));
}

TEST(FileExportTests, WriteAggregatedStatsCsvCreatesDeterministicFile) {
  const auto root = temp_root();
  const auto path = root / "aggregate.csv";
  const std::vector<AggregatedStrategyStats> stats{stats_row()};

  const FileWriteResult a = write_aggregated_stats_csv(path.string(), stats);
  const std::string first = read_file(path);
  const FileWriteResult b = write_aggregated_stats_csv(path.string(), stats);

  EXPECT_TRUE_MSG(a.ok, a.reason);
  EXPECT_TRUE_MSG(b.ok, b.reason);
  EXPECT_EQ(first, read_file(path));
}

TEST(FileExportTests, WriteRunSummaryJsonCreatesDeterministicFile) {
  const auto root = temp_root();
  const auto path = root / "summary.json";
  const ResearchRunResult summary = run_summary();

  const FileWriteResult a = write_run_summary_json(path.string(), summary);
  const std::string first = read_file(path);
  const FileWriteResult b = write_run_summary_json(path.string(), summary);

  EXPECT_TRUE_MSG(a.ok, a.reason);
  EXPECT_TRUE_MSG(b.ok, b.reason);
  EXPECT_EQ(first, read_file(path));
}

TEST(FileExportTests, WriteResearchBundleCreatesExpectedFiles) {
  const auto root = temp_root() / "bundle";

  const FileWriteResult result = write_research_bundle(root.string(), {ranked_row(1, "mm", 10, 7)}, {stats_row()}, run_summary());

  EXPECT_TRUE_MSG(result.ok, result.reason);
  EXPECT_TRUE(std::filesystem::exists(root / "ranking.csv"));
  EXPECT_TRUE(std::filesystem::exists(root / "aggregate.csv"));
  EXPECT_TRUE(std::filesystem::exists(root / "summary.json"));
  EXPECT_TRUE(!read_file(root / "summary.json").empty());
}

TEST(FileExportTests, WriteRejectsInvalidPath) {
  const auto root = temp_root();
  const auto dir_path = root / "directory_target";
  std::filesystem::create_directories(dir_path);

  const FileWriteResult result = write_text_file(dir_path.string(), "x");

  EXPECT_FALSE(result.ok);
  EXPECT_TRUE(result.reason.find("failed") != std::string::npos);
}

TEST(FileExportTests, FileExportMatchesStringExport) {
  const auto root = temp_root();
  const auto path = root / "ranking.csv";
  const std::vector<RankedStrategyResult> ranked{ranked_row(1, "mm", 10, 7), ranked_row(2, "taker", 20, 3)};

  const FileWriteResult result = write_ranked_results_csv(path.string(), ranked);

  EXPECT_TRUE_MSG(result.ok, result.reason);
  EXPECT_EQ(read_file(path), export_ranked_results_csv(ranked));
}
