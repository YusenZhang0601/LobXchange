#include "lobx/agents/agent_factory.hpp"
#include "lobx/simulation/agent_runtime.hpp"
#include "lobx/simulation/price_series_recorder.hpp"

#include "test_helpers/test_framework.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace lobx::agents;
using namespace lobx::simulation;

namespace {

struct PriceImpactScenario {
  std::string name;
  std::uint64_t seed{42};
  int agent_count{0};
  int steps{0};
  double static_market_maker_ratio{0.0};
  double noise_trader_ratio{0.0};
  double momentum_follower_ratio{0.0};
  double mean_reverter_ratio{0.0};
  double whale_sweeper_ratio{0.0};
};

struct ScenarioRunResult {
  PriceImpactSummary summary;
  AccountingSummary accounting;
  std::filesystem::path output_dir;
};

PriceImpactScenario baseline_small() {
  return PriceImpactScenario{"baseline_liquid_market", 42, 20, 30, 0.70, 0.30, 0.0, 0.0, 0.0};
}

PriceImpactScenario momentum_dislocation_small() {
  return PriceImpactScenario{"momentum_price_dislocation", 43, 30, 40, 0.50, 0.25, 0.25, 0.0, 0.0};
}

PriceImpactScenario whale_sweep_small() {
  return PriceImpactScenario{"whale_sweep_impact", 44, 30, 50, 0.60, 0.30, 0.0, 0.0, 0.10};
}

PriceImpactScenario mean_reversion_stabilizer_small() {
  return PriceImpactScenario{"mean_reversion_stabilizer", 45, 30, 40, 0.45, 0.20, 0.20, 0.15, 0.0};
}

PriceImpactScenario baseline_large() {
  return PriceImpactScenario{"baseline_liquid_market", 4201, 100, 100, 0.70, 0.30, 0.0, 0.0, 0.0};
}

PriceImpactScenario momentum_dislocation_large() {
  return PriceImpactScenario{"momentum_price_dislocation", 4301, 100, 100, 0.50, 0.25, 0.25, 0.0, 0.0};
}

PriceImpactScenario whale_sweep_large() {
  return PriceImpactScenario{"whale_sweep_impact", 4401, 100, 100, 0.60, 0.30, 0.0, 0.0, 0.10};
}

PriceImpactScenario mean_reversion_stabilizer_large() {
  return PriceImpactScenario{"mean_reversion_stabilizer", 4501, 100, 100, 0.45, 0.20, 0.20, 0.15, 0.0};
}

bool env_enabled(const char* name) {
  const char* value = std::getenv(name);
  return value != nullptr && std::string(value) == "1";
}

std::filesystem::path output_root() {
  if (const char* value = std::getenv("LOBX_PRICE_IMPACT_OUTPUT_DIR")) return value;
  return std::filesystem::temp_directory_path() / "lobx_price_impact_runs";
}

bool file_exists(const std::filesystem::path& path) {
  return std::filesystem::exists(path) && std::filesystem::is_regular_file(path);
}

int count_for(const PriceImpactScenario& scenario, double ratio, int assigned_so_far, bool last) {
  const int remaining = std::max(0, scenario.agent_count - assigned_so_far);
  if (last) return remaining;
  const int requested = static_cast<int>(std::llround(static_cast<double>(scenario.agent_count) * ratio));
  return std::clamp(requested, 0, remaining);
}

void add_agents(AgentRuntime& runtime,
                AgentFactoryRegistry& registry,
                const std::string& type,
                int count,
                AgentId& next_agent_id,
                AgentGroupId group_id,
                std::uint64_t scenario_seed) {
  for (int i = 0; i < count; ++i) {
    AgentConfig config{};
    config.type = type;
    config.group_id = group_id;
    config.numeric_params["seed"] = static_cast<double>(scenario_seed + next_agent_id * 131 + static_cast<std::uint64_t>(i));
    config.numeric_params["reference_price"] = 100.0;
    if (type == "whale_sweeper") {
      config.numeric_params["interval"] = 12.0;
      config.numeric_params["quantity"] = 20.0;
    }
    runtime.add_agent(registry.create(type, next_agent_id++, config));
  }
}

ScenarioRunResult run_price_impact_scenario(const PriceImpactScenario& scenario, const std::filesystem::path& root) {
  const std::filesystem::path out_dir = root / scenario.name / std::to_string(scenario.seed);
  std::filesystem::remove_all(out_dir);
  std::filesystem::create_directories(out_dir);

  AgentRuntimeConfig runtime_config{};
  runtime_config.steps = scenario.steps;
  runtime_config.reference_price = 100;
  runtime_config.book_levels = 10;
  runtime_config.recent_trade_limit = 256;
  AgentRuntime runtime(runtime_config);

  AgentFactoryRegistry registry;
  register_builtin_agents(registry);

  int assigned = 0;
  const int makers = count_for(scenario, scenario.static_market_maker_ratio, assigned, false);
  assigned += makers;
  const int noise = count_for(scenario, scenario.noise_trader_ratio, assigned, false);
  assigned += noise;
  const int momentum = count_for(scenario, scenario.momentum_follower_ratio, assigned, false);
  assigned += momentum;
  const int mean_reversion = count_for(scenario, scenario.mean_reverter_ratio, assigned, false);
  assigned += mean_reversion;
  const int whales = count_for(scenario, scenario.whale_sweeper_ratio, assigned, true);
  assigned += whales;
  EXPECT_EQ(assigned, scenario.agent_count);

  AgentId next_agent_id = 100;
  add_agents(runtime, registry, "static_market_maker", makers, next_agent_id, 1, scenario.seed);
  add_agents(runtime, registry, "noise_trader", noise, next_agent_id, 2, scenario.seed);
  add_agents(runtime, registry, "momentum_follower", momentum, next_agent_id, 3, scenario.seed);
  add_agents(runtime, registry, "mean_reverter", mean_reversion, next_agent_id, 4, scenario.seed);
  add_agents(runtime, registry, "whale_sweeper", whales, next_agent_id, 5, scenario.seed);

  PriceSeriesRecorder recorder;
  recorder.record(0, runtime);
  for (int step = 1; step <= scenario.steps; ++step) {
    runtime.step();
    recorder.record(step, runtime);
  }

  PriceImpactSummary summary = recorder.summarize(scenario.name, scenario.seed, scenario.agent_count, scenario.steps);
  AccountingSummary accounting = runtime.accounting_summary(runtime.market_view().mid_price);
  EXPECT_TRUE(recorder.write_price_series_csv((out_dir / "price_series.csv").string()));
  EXPECT_TRUE(recorder.write_summary_json((out_dir / "summary.json").string(), summary, accounting));
  return ScenarioRunResult{summary, accounting, out_dir};
}

void expect_valid_summary(const ScenarioRunResult& result) {
  EXPECT_TRUE(file_exists(result.output_dir / "price_series.csv"));
  EXPECT_TRUE(file_exists(result.output_dir / "summary.json"));
  EXPECT_TRUE(file_exists(result.output_dir / "accounting_summary.json"));
  EXPECT_TRUE(result.summary.price_samples_count > 0);
  EXPECT_TRUE(std::isfinite(result.summary.final_mid_price));
  EXPECT_TRUE(result.summary.final_mid_price > 0.0);
  EXPECT_TRUE(std::isfinite(result.summary.total_return_bps));
  EXPECT_TRUE(std::isfinite(result.summary.realized_vol_bps));
  EXPECT_TRUE(std::isfinite(result.summary.max_drawdown_bps));
  EXPECT_TRUE(std::isfinite(result.summary.largest_abs_return_bps));
  EXPECT_TRUE(std::isfinite(result.accounting.system_pnl_residual));
  EXPECT_EQ(result.accounting.agent_count, result.summary.agent_count);
  EXPECT_TRUE_MSG(std::abs(result.accounting.system_pnl_residual) <= 0.0,
                  "system_pnl_residual=" + std::to_string(result.accounting.system_pnl_residual) +
                      " fee_revenue=" + std::to_string(result.accounting.exchange_fee_revenue) +
                      " negative_agents=" + std::to_string(result.accounting.negative_pnl_agent_count) +
                      " agent_count=" + std::to_string(result.accounting.agent_count));
  EXPECT_FALSE_MSG(result.accounting.negative_pnl_agent_count == result.accounting.agent_count,
                   "all agents have negative total-equity PnL; fee_revenue=" +
                       std::to_string(result.accounting.exchange_fee_revenue) +
                       " residual=" + std::to_string(result.accounting.system_pnl_residual) +
                       " final_mid=" + std::to_string(result.summary.final_mid_price));
}

} // namespace

TEST(AgentPriceImpactTests, SmokeScenariosExportPriceSeriesSummaryAndComparison) {
  std::vector<PriceImpactSummary> summaries;
  for (const PriceImpactScenario& scenario : {baseline_small(),
                                             momentum_dislocation_small(),
                                             whale_sweep_small(),
                                             mean_reversion_stabilizer_small()}) {
    const ScenarioRunResult result = run_price_impact_scenario(scenario, output_root());
    expect_valid_summary(result);
    summaries.push_back(result.summary);
  }

  const std::filesystem::path comparison = output_root() / "scenario_comparison.csv";
  std::filesystem::create_directories(comparison.parent_path());
  EXPECT_TRUE(write_scenario_comparison_csv(comparison.string(), summaries));
  EXPECT_TRUE(file_exists(comparison));

  bool saw_movement = false;
  for (const PriceImpactSummary& summary : summaries) {
    saw_movement = saw_movement || std::abs(summary.total_return_bps) > 0.0 || summary.largest_abs_return_bps > 0.0;
  }
  EXPECT_TRUE_MSG(saw_movement, "expected at least one scenario to record non-zero price movement");
}

TEST(AgentPriceImpactTests, LargePriceImpactExperimentsAreExplicitlyGated) {
  if (!env_enabled("LOBX_RUN_LARGE_PRICE_IMPACT")) {
    std::cout << "[SKIP] Set LOBX_RUN_LARGE_PRICE_IMPACT=1 to run large price impact experiments\n";
    return;
  }

  const std::filesystem::path root = output_root();
  std::vector<PriceImpactSummary> summaries;
  for (const PriceImpactScenario& scenario : {baseline_large(),
                                             momentum_dislocation_large(),
                                             whale_sweep_large(),
                                             mean_reversion_stabilizer_large()}) {
    ScenarioRunResult result = run_price_impact_scenario(scenario, root);
    expect_valid_summary(result);
    summaries.push_back(result.summary);
  }
  EXPECT_TRUE(write_scenario_comparison_csv((root / "scenario_comparison.csv").string(), summaries));
}
