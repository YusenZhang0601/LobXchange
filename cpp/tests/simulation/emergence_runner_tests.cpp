#include "lobx/simulation/emergence_export.hpp"
#include "lobx/simulation/emergence_runner.hpp"

#include "test_helpers/test_framework.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace lobx::sim;

namespace {

EmergenceConfig config(int makers = 2, int ticks = 40, int warmup = 5) {
  MarketEnvironmentConfig env{};
  env.market_symbol = "BTC-USDT";
  env.reference_price = 100;
  env.ticks = ticks;
  env.warmup_ticks = warmup;
  env.initial_book = {InitialBookLevel{lob::Side::Bid, 99, 3}, InitialBookLevel{lob::Side::Ask, 101, 3}};

  AgentGroupConfig maker{};
  maker.strategy_type = "market_maker";
  maker.count = makers;
  maker.name_prefix = "mm";
  maker.latency_range = LatencyRangeConfig{0, 0, 0, 0, 1, 1, 1, 1};
  maker.param_ranges = {{"bid_px", DoubleRange{98, 99}}, {"ask_px", DoubleRange{101, 102}}, {"qty", DoubleRange{1, 2}}};

  AgentGroupConfig taker{};
  taker.strategy_type = "taker_sweep";
  taker.count = 1;
  taker.name_prefix = "taker";
  taker.latency_range = LatencyRangeConfig{0, 0, 0, 0, 1, 1, 1, 1};
  taker.param_ranges = {{"side", DoubleRange{0, 0}}, {"target_qty", DoubleRange{1, 1}}, {"limit_price", DoubleRange{101, 101}}, {"max_avg_price", DoubleRange{101, 101}}};

  return EmergenceConfig{env, AgentPopulationConfig{77, 10, {maker, taker}}};
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

} // namespace

TEST(EmergenceRunnerTests, EmergenceRunnerSameSeedDeterministic) {
  EmergenceRunner runner;
  const EmergenceConfig cfg = config();

  const EmergenceRunResult a = runner.run(cfg);
  const EmergenceRunResult b = runner.run(cfg);

  EXPECT_TRUE_MSG(a.ok, a.reason);
  EXPECT_TRUE_MSG(b.ok, b.reason);
  EXPECT_TRUE(same_research_result(a.research_result, b.research_result));
  EXPECT_EQ(export_emergence_summary_json(a.metrics), export_emergence_summary_json(b.metrics));
}

TEST(EmergenceRunnerTests, EmergenceRunnerMaintainsExchangeInvariants) {
  EmergenceRunner runner;
  const EmergenceRunResult result = runner.run(config());

  EXPECT_TRUE_MSG(result.ok, result.reason);
  EXPECT_TRUE(result.ledger_invariant_ok);
  EXPECT_TRUE(result.book_open_consistency_ok);
  EXPECT_TRUE(result.no_private_data_leak);
  EXPECT_TRUE(result.no_future_public_data_leak);
}

TEST(EmergenceRunnerTests, EmergenceRunnerProducesPriceSeries) {
  EmergenceRunner runner;
  const EmergenceRunResult result = runner.run(config());

  EXPECT_TRUE_MSG(result.ok, result.reason);
  EXPECT_TRUE(!result.metrics.samples.empty());
  EXPECT_TRUE(export_price_series_csv(result.metrics).find("mid_price") != std::string::npos);
}

TEST(EmergenceRunnerTests, EmergenceRunnerProducesAgentMetrics) {
  EmergenceRunner runner;
  const EmergenceRunResult result = runner.run(config());

  EXPECT_TRUE_MSG(result.ok, result.reason);
  EXPECT_TRUE(!result.metrics.agent_metrics.empty());
  EXPECT_TRUE(export_agent_metrics_csv(result.metrics).find("bot_name") != std::string::npos);
}

TEST(EmergenceRunnerTests, EmergenceRunnerExportsBundle) {
  const std::filesystem::path root = std::filesystem::temp_directory_path() / "lobx_exchange_emergence_bundle";
  std::filesystem::remove_all(root);

  EmergenceRunner runner;
  const EmergenceRunResult result = runner.run(config());
  const FileWriteResult write = write_emergence_bundle(root.string(), result.metrics);

  EXPECT_TRUE_MSG(write.ok, write.reason);
  EXPECT_TRUE(std::filesystem::exists(root / "emergence_summary.json"));
  EXPECT_TRUE(std::filesystem::exists(root / "price_series.csv"));
  EXPECT_TRUE(std::filesystem::exists(root / "spread_series.csv"));
  EXPECT_TRUE(std::filesystem::exists(root / "depth_series.csv"));
  EXPECT_TRUE(std::filesystem::exists(root / "agent_metrics.csv"));
  EXPECT_TRUE(read_file(root / "emergence_summary.json").find("realized_volatility") != std::string::npos);
}

TEST(EmergenceRunnerTests, EmergenceRunnerRejectsInvalidMarketEnvironment) {
  EmergenceConfig cfg = config();
  cfg.market_environment.ticks = 0;

  const EmergenceRunResult result = EmergenceRunner().run(cfg);

  EXPECT_FALSE(result.ok);
}

TEST(EmergenceRunnerTests, EmergenceRunnerRejectsInvalidAgentPopulation) {
  EmergenceConfig cfg = config();
  cfg.agent_population.groups.clear();

  const EmergenceRunResult result = EmergenceRunner().run(cfg);

  EXPECT_FALSE(result.ok);
}

TEST(EmergenceRunnerTests, EmergenceRunnerInitialBookCreatesDepth) {
  EmergenceRunResult result = EmergenceRunner().run(config());

  EXPECT_TRUE_MSG(result.ok, result.reason);
  EXPECT_TRUE(result.metrics.mean_depth_top5 > 0.0L);
}

TEST(EmergenceRunnerTests, EmergenceRunnerWarmupExcludedFromMetrics) {
  EmergenceRunResult result = EmergenceRunner().run(config(2, 30, 10));

  EXPECT_TRUE_MSG(result.ok, result.reason);
  EXPECT_EQ(result.metrics.measured_ticks, 20);
  EXPECT_EQ(result.metrics.samples.front().tick, 11);
}
