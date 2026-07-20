#include "lobx/simulation/config_loader.hpp"
#include "lobx/simulation/file_export.hpp"
#include "lobx/simulation/research_runner.hpp"

#include "test_helpers/test_framework.hpp"

#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace lobx::sim;

namespace {

std::string scenario_json() {
  return R"JSON({
    "seed": 42,
    "ticks": 30,
    "market_symbol": "BTC-USDT",
    "bots": [
      {
        "user": 10,
        "name": "mm",
        "strategy_type": "market_maker",
        "latency": {"order": 0, "cancel": 0, "market_data": 1, "private_data": 1},
        "params": {"bid_px": 99, "ask_px": 101, "qty": 1}
      },
      {
        "user": 20,
        "name": "taker",
        "strategy_type": "taker_sweep",
        "latency": {"order": 0, "cancel": 0, "market_data": 1, "private_data": 1},
        "params": {"side": 0, "target_qty": 1, "limit_price": 101, "max_avg_price": 101}
      },
      {
        "user": 40,
        "name": "noise",
        "strategy_type": "noise_trader",
        "latency": {"order": 1, "cancel": 0, "market_data": 1, "private_data": 1},
        "params": {}
      }
    ]
  })JSON";
}

std::string sweep_json() {
  return R"JSON({
    "params": [
      {"bot_name": "mm", "param_name": "bid_px", "values": [98, 99]},
      {"bot_name": "mm", "param_name": "ask_px", "values": [101, 102]}
    ]
  })JSON";
}

std::string seed_json() {
  return R"JSON({"seeds": [1, 2, 3]})JSON";
}

std::filesystem::path temp_root() {
  const std::filesystem::path root = std::filesystem::temp_directory_path() / "lobx_exchange_research_config_workflow";
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

bool same_params(const std::map<std::string, double>& a, const std::map<std::string, double>& b) {
  return a == b;
}

} // namespace

TEST(ResearchConfigWorkflow, LoadScenarioSweepSeedsRunRankAggregateAndExport) {
  const ScenarioConfigLoadResult scenario = load_scenario_config_from_json_string(scenario_json());
  const SweepConfigLoadResult sweep = load_sweep_config_from_json_string(sweep_json());
  const MultiSeedConfigLoadResult seeds = load_seed_config_from_json_string(seed_json());

  EXPECT_TRUE_MSG(scenario.ok, scenario.reason);
  EXPECT_TRUE_MSG(sweep.ok, sweep.reason);
  EXPECT_TRUE_MSG(seeds.ok, seeds.reason);

  const ValidationResult validation = validate_scenario_config(scenario.config);
  EXPECT_TRUE_MSG(validation.ok, validation.reason);

  ResearchRunner runner;
  const std::vector<SweepRun> sweep_runs = run_parameter_sweep(runner, scenario.config, sweep.params);
  const std::vector<RankedStrategyResult> ranked = rank_sweep_results(sweep_runs, "mm", RankingMetric::NetPnl);

  EXPECT_EQ(sweep_runs.size(), 4UL);
  EXPECT_TRUE(!ranked.empty());
  if (ranked.empty()) return;
  EXPECT_EQ(ranked.front().rank, 1);

  ScenarioConfig top_config = sweep_runs.front().config;
  for (const SweepRun& run : sweep_runs) {
    if (same_params(run.config.bots.front().params, ranked.front().params)) {
      top_config = run.config;
      break;
    }
  }

  const std::vector<SeedEvaluationRun> multi_seed = run_multi_seed_evaluation(runner, top_config, seeds.seeds);
  const AggregatedStrategyStats stats = aggregate_strategy_stats(multi_seed, "mm");

  EXPECT_EQ(multi_seed.size(), seeds.seeds.size());
  EXPECT_EQ(stats.runs, static_cast<int>(seeds.seeds.size()));

  const auto output_dir = temp_root() / "bundle";
  const FileWriteResult written = write_research_bundle(output_dir.string(), ranked, {stats}, multi_seed.front().result);

  EXPECT_TRUE_MSG(written.ok, written.reason);
  EXPECT_TRUE(std::filesystem::exists(output_dir / "ranking.csv"));
  EXPECT_TRUE(std::filesystem::exists(output_dir / "aggregate.csv"));
  EXPECT_TRUE(std::filesystem::exists(output_dir / "summary.json"));
  EXPECT_TRUE(read_file(output_dir / "ranking.csv").find("rank,bot_name,user") != std::string::npos);
  EXPECT_TRUE(read_file(output_dir / "summary.json").find("\"invariants\"") != std::string::npos);

  for (const SeedEvaluationRun& run : multi_seed) {
    EXPECT_TRUE(run.result.ledger_invariant_ok);
    EXPECT_TRUE(run.result.book_open_consistency_ok);
  }
}
