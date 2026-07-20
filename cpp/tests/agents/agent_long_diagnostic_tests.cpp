#include "lobx/simulation/diagnostic_experiment.hpp"

#include "test_helpers/test_framework.hpp"

#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace lobx::simulation;

namespace {

bool env_enabled(const char* name) {
  const char* value = std::getenv(name);
  return value != nullptr && std::string(value) == "1";
}

std::string env_string(const char* name, const std::string& fallback) {
  const char* value = std::getenv(name);
  return value == nullptr || *value == '\0' ? fallback : std::string(value);
}

bool should_run_legacy_scenarios() {
  const std::string mode = env_string("LOBX_DIAG_SCENARIOS", "legacy");
  return mode == "legacy" || mode == "all";
}

bool should_run_bounded_scenarios() {
  const std::string mode = env_string("LOBX_DIAG_SCENARIOS", "legacy");
  return mode == "bounded" || mode == "all";
}

bool file_exists(const std::filesystem::path& path) {
  return std::filesystem::exists(path) && std::filesystem::is_regular_file(path);
}

int csv_data_rows(const std::filesystem::path& path) {
  std::ifstream in(path);
  int lines = 0;
  std::string line;
  while (std::getline(in, line)) ++lines;
  return std::max(0, lines - 1);
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream in(path);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

double json_number(const std::string& json, const std::string& key) {
  const std::string needle = "\"" + key + "\":";
  const std::size_t pos = json.find(needle);
  if (pos == std::string::npos) return std::numeric_limits<double>::quiet_NaN();
  const std::size_t start = pos + needle.size();
  char* end = nullptr;
  return std::strtod(json.c_str() + start, &end);
}

void expect_required_bundle_files(const std::filesystem::path& dir) {
  for (const char* file : {"run_metadata.json",
                           "summary.json",
                           "accounting_summary.json",
                           "inventory_consistency_by_agent.csv",
                           "inventory_consistency_summary.json",
                           "price_series.csv",
                           "agent_state_samples.csv",
                           "agent_final_state.csv",
                           "agent_type_summary.csv",
                           "agent_type_pnl_timeseries.csv",
                           "actions_by_step.csv",
                           "actions_by_type.csv",
                           "trades_by_step.csv",
                           "book_samples.csv",
                           "open_order_growth.csv",
                           "runtime_metrics.csv",
                           "perf_summary.json",
                           "run_hash.json",
                           "unit_sanity_summary.json",
                           "diagnostic_warnings.json"}) {
    EXPECT_TRUE_MSG(file_exists(dir / file), std::string("missing diagnostic output ") + file);
  }
}

std::string csv_header(const std::filesystem::path& path) {
  std::ifstream in(path);
  std::string line;
  std::getline(in, line);
  return line;
}

std::vector<std::string> split_csv_line(const std::string& line) {
  std::vector<std::string> cells;
  std::stringstream ss(line);
  std::string cell;
  while (std::getline(ss, cell, ',')) cells.push_back(cell);
  return cells;
}

std::map<std::string, std::size_t> csv_columns(const std::filesystem::path& path) {
  std::map<std::string, std::size_t> columns;
  const auto header = split_csv_line(csv_header(path));
  for (std::size_t i = 0; i < header.size(); ++i) columns[header[i]] = i;
  return columns;
}

double last_csv_number(const std::filesystem::path& path, const std::string& column) {
  const auto columns = csv_columns(path);
  const auto it = columns.find(column);
  if (it == columns.end()) return std::numeric_limits<double>::quiet_NaN();
  std::ifstream in(path);
  std::string line;
  std::getline(in, line);
  std::string last;
  while (std::getline(in, line)) {
    if (!line.empty()) last = line;
  }
  const auto cells = split_csv_line(last);
  if (it->second >= cells.size()) return std::numeric_limits<double>::quiet_NaN();
  return std::strtod(cells[it->second].c_str(), nullptr);
}

double max_csv_number(const std::filesystem::path& path, const std::string& column) {
  const auto columns = csv_columns(path);
  const auto it = columns.find(column);
  if (it == columns.end()) return std::numeric_limits<double>::quiet_NaN();
  std::ifstream in(path);
  std::string line;
  std::getline(in, line);
  double max_value = 0.0;
  while (std::getline(in, line)) {
    const auto cells = split_csv_line(line);
    if (it->second < cells.size()) max_value = std::max(max_value, std::strtod(cells[it->second].c_str(), nullptr));
  }
  return max_value;
}

double max_open_orders_for_agent_type(const std::filesystem::path& path, const std::string& agent_type) {
  const auto columns = csv_columns(path);
  const auto type_it = columns.find("agent_type");
  const auto open_it = columns.find("open_order_count");
  if (type_it == columns.end() || open_it == columns.end()) return std::numeric_limits<double>::quiet_NaN();
  std::ifstream in(path);
  std::string line;
  std::getline(in, line);
  double max_value = 0.0;
  while (std::getline(in, line)) {
    const auto cells = split_csv_line(line);
    if (type_it->second < cells.size() && open_it->second < cells.size() && cells[type_it->second] == agent_type) {
      max_value = std::max(max_value, std::strtod(cells[open_it->second].c_str(), nullptr));
    }
  }
  return max_value;
}

std::string json_string(const std::string& json, const std::string& key) {
  const std::string needle = "\"" + key + "\":\"";
  const std::size_t pos = json.find(needle);
  if (pos == std::string::npos) return {};
  const std::size_t start = pos + needle.size();
  const std::size_t end = json.find('"', start);
  return end == std::string::npos ? std::string{} : json.substr(start, end - start);
}

void expect_runtime_elapsed_monotonic(const std::filesystem::path& path) {
  std::ifstream in(path);
  std::string line;
  std::getline(in, line);
  double previous = -1.0;
  while (std::getline(in, line)) {
    std::stringstream ss(line);
    std::string cell;
    int column = 0;
    double elapsed = 0.0;
    while (std::getline(ss, cell, ',')) {
      if (column == 2) {
        elapsed = std::strtod(cell.c_str(), nullptr);
        break;
      }
      ++column;
    }
    EXPECT_TRUE_MSG(elapsed >= previous, "runtime elapsed_ms must be monotonic");
    previous = elapsed;
  }
}

void expect_valid_bundle(const DiagnosticScenario& scenario, const DiagnosticRunResult& result) {
  EXPECT_TRUE(std::filesystem::exists(result.output_dir));
  expect_required_bundle_files(result.output_dir);
  EXPECT_TRUE(csv_data_rows(result.output_dir / "price_series.csv") > 0);
  EXPECT_EQ(csv_data_rows(result.output_dir / "agent_final_state.csv"), scenario.agent_count);
  EXPECT_TRUE(csv_data_rows(result.output_dir / "agent_state_samples.csv") > scenario.agent_count);
  EXPECT_TRUE(result.final_mid_price > 0.0);
  EXPECT_TRUE(std::isfinite(result.system_pnl_residual));
  EXPECT_TRUE_MSG(std::abs(result.system_pnl_residual) <= 0.0,
                  "zero-fee diagnostic residual must be zero");

  const std::string accounting = read_file(result.output_dir / "accounting_summary.json");
  const double negative_count = json_number(accounting, "negative_pnl_agent_count");
  const double agent_count = json_number(accounting, "agent_count");
  const double fee_revenue = json_number(accounting, "exchange_fee_revenue");
  EXPECT_FALSE_MSG(negative_count == agent_count && fee_revenue == 0.0,
                   "all agents have negative total-equity PnL in zero-fee diagnostic run");
  expect_runtime_elapsed_monotonic(result.output_dir / "runtime_metrics.csv");

  const std::string inventory = read_file(result.output_dir / "inventory_consistency_summary.json");
  EXPECT_TRUE(std::isfinite(json_number(inventory, "max_abs_inventory_residual")));
  EXPECT_TRUE_MSG(json_number(inventory, "max_abs_inventory_residual") <= 0.0,
                  "inventory residual must be zero in closed-system diagnostic run");
  EXPECT_EQ(static_cast<int>(json_number(inventory, "failed_agent_count")), 0);

  EXPECT_TRUE(csv_data_rows(result.output_dir / "open_order_growth.csv") > 0);
  EXPECT_TRUE(csv_data_rows(result.output_dir / "agent_type_pnl_timeseries.csv") > 0);
  EXPECT_TRUE(std::isfinite(max_csv_number(result.output_dir / "open_order_growth.csv", "total_open_orders")));
  EXPECT_TRUE(std::isfinite(last_csv_number(result.output_dir / "agent_type_pnl_timeseries.csv", "sum_pnl")));

  const std::string runtime_header = csv_header(result.output_dir / "runtime_metrics.csv");
  EXPECT_TRUE(runtime_header.find("agent_decide_ms") != std::string::npos);
  EXPECT_TRUE(runtime_header.find("exchange_apply_ms") != std::string::npos);
  EXPECT_TRUE(runtime_header.find("agents_skipped_count") != std::string::npos);

  const std::string perf = read_file(result.output_dir / "perf_summary.json");
  EXPECT_TRUE(json_number(perf, "total_elapsed_ms") > 0.0);
}

} // namespace

TEST(AgentLongDiagnosticTests, BalancedMixedAgentsLong) {
  if (!env_enabled("LOBX_RUN_LONG_DIAGNOSTIC")) {
    std::cout << "[SKIP] Set LOBX_RUN_LONG_DIAGNOSTIC=1 to run long diagnostic experiments\n";
    return;
  }
  if (!should_run_legacy_scenarios()) {
    std::cout << "[SKIP] LOBX_DIAG_SCENARIOS excludes legacy scenarios\n";
    return;
  }
  DiagnosticScenario scenario = apply_diagnostic_env_overrides(balanced_mixed_agents_long());
  const DiagnosticRunResult result = run_diagnostic_experiment(scenario,
                                                               diagnostic_output_options_from_env(),
                                                               diagnostic_output_root_from_env());
  expect_valid_bundle(scenario, result);
}

TEST(AgentLongDiagnosticTests, StressWhaleMomentumLong) {
  if (!env_enabled("LOBX_RUN_LONG_DIAGNOSTIC")) {
    std::cout << "[SKIP] Set LOBX_RUN_LONG_DIAGNOSTIC=1 to run long diagnostic experiments\n";
    return;
  }
  if (!should_run_legacy_scenarios()) {
    std::cout << "[SKIP] LOBX_DIAG_SCENARIOS excludes legacy scenarios\n";
    return;
  }
  DiagnosticScenario scenario = apply_diagnostic_env_overrides(stress_whale_momentum_long());
  const DiagnosticRunResult result = run_diagnostic_experiment(scenario,
                                                               diagnostic_output_options_from_env(),
                                                               diagnostic_output_root_from_env());
  expect_valid_bundle(scenario, result);
}

TEST(AgentLongDiagnosticTests, BalancedMixedAgentsBoundedLong) {
  if (!env_enabled("LOBX_RUN_LONG_DIAGNOSTIC")) {
    std::cout << "[SKIP] Set LOBX_RUN_LONG_DIAGNOSTIC=1 to run long diagnostic experiments\n";
    return;
  }
  if (!should_run_bounded_scenarios()) {
    std::cout << "[SKIP] LOBX_DIAG_SCENARIOS excludes bounded scenarios\n";
    return;
  }
  DiagnosticScenario scenario = apply_diagnostic_env_overrides(balanced_mixed_agents_bounded_long());
  const DiagnosticRunResult result = run_diagnostic_experiment(scenario,
                                                               diagnostic_output_options_from_env(),
                                                               diagnostic_output_root_from_env());
  expect_valid_bundle(scenario, result);
  EXPECT_TRUE_MSG(max_open_orders_for_agent_type(result.output_dir / "agent_final_state.csv", "static_market_maker") <= 2.0,
                  "bounded market makers should cap static-maker open orders");
  EXPECT_TRUE_MSG(max_csv_number(result.output_dir / "open_order_growth.csv", "total_open_orders") <
                      static_cast<double>(scenario.agent_count * std::max(100, scenario.steps / 20)),
                  "bounded diagnostic aggregate open orders should stay finite under scaled guardrail");
  EXPECT_TRUE_MSG(last_csv_number(result.output_dir / "runtime_metrics.csv", "agents_skipped_count") > 0.0,
                  "bounded scheduler should skip non-due agents");
}

TEST(AgentLongDiagnosticTests, StressWhaleMomentumBoundedLong) {
  if (!env_enabled("LOBX_RUN_LONG_DIAGNOSTIC")) {
    std::cout << "[SKIP] Set LOBX_RUN_LONG_DIAGNOSTIC=1 to run long diagnostic experiments\n";
    return;
  }
  if (!should_run_bounded_scenarios()) {
    std::cout << "[SKIP] LOBX_DIAG_SCENARIOS excludes bounded scenarios\n";
    return;
  }
  DiagnosticScenario scenario = apply_diagnostic_env_overrides(stress_whale_momentum_bounded_long());
  const DiagnosticRunResult result = run_diagnostic_experiment(scenario,
                                                               diagnostic_output_options_from_env(),
                                                               diagnostic_output_root_from_env());
  expect_valid_bundle(scenario, result);
  EXPECT_TRUE_MSG(max_open_orders_for_agent_type(result.output_dir / "agent_final_state.csv", "static_market_maker") <= 2.0,
                  "bounded market makers should cap static-maker open orders");
  EXPECT_TRUE_MSG(max_csv_number(result.output_dir / "open_order_growth.csv", "total_open_orders") <
                      static_cast<double>(scenario.agent_count * std::max(100, scenario.steps / 20)),
                  "bounded diagnostic aggregate open orders should stay finite under scaled guardrail");
  EXPECT_TRUE_MSG(last_csv_number(result.output_dir / "runtime_metrics.csv", "agents_skipped_count") > 0.0,
                  "bounded scheduler should skip non-due agents");
}

TEST(AgentDiagnosticInvariantTests, SameSeedDeterministicHash) {
  DiagnosticScenario scenario = balanced_mixed_agents_bounded_long();
  scenario.name = "deterministic_hash";
  scenario.agent_count = 8;
  scenario.steps = 80;
  scenario.sample_interval = 10;
  scenario.seed = 777;
  const auto root = std::filesystem::path("build-bench") / "diagnostic_invariant_runs";
  DiagnosticRunResult first = run_diagnostic_experiment(scenario, DiagnosticOutputOptions{}, root / "first");
  DiagnosticRunResult second = run_diagnostic_experiment(scenario, DiagnosticOutputOptions{}, root / "second");

  const std::string h1 = read_file(first.output_dir / "run_hash.json");
  const std::string h2 = read_file(second.output_dir / "run_hash.json");
  EXPECT_EQ(json_string(h1, "price_series_hash"), json_string(h2, "price_series_hash"));
  EXPECT_EQ(json_string(h1, "agent_final_state_hash"), json_string(h2, "agent_final_state_hash"));
  EXPECT_EQ(json_string(h1, "accounting_summary_hash"), json_string(h2, "accounting_summary_hash"));
}

TEST(AgentDiagnosticInvariantTests, DifferentSeedChangesHash) {
  DiagnosticScenario a = balanced_mixed_agents_bounded_long();
  a.name = "different_seed_hash";
  a.agent_count = 8;
  a.steps = 80;
  a.sample_interval = 10;
  a.seed = 778;
  DiagnosticScenario b = a;
  b.seed = 779;
  const auto root = std::filesystem::path("build-bench") / "diagnostic_invariant_runs";
  DiagnosticRunResult first = run_diagnostic_experiment(a, DiagnosticOutputOptions{}, root / "seed_a");
  DiagnosticRunResult second = run_diagnostic_experiment(b, DiagnosticOutputOptions{}, root / "seed_b");

  const std::string h1 = read_file(first.output_dir / "run_hash.json");
  const std::string h2 = read_file(second.output_dir / "run_hash.json");
  EXPECT_TRUE(json_string(h1, "price_series_hash") != json_string(h2, "price_series_hash") ||
              json_string(h1, "agent_final_state_hash") != json_string(h2, "agent_final_state_hash"));
}

TEST(AgentDiagnosticInvariantTests, SamplingIntervalDoesNotAffectFinalState) {
  DiagnosticScenario a = balanced_mixed_agents_bounded_long();
  a.name = "sampling_consistency";
  a.agent_count = 8;
  a.steps = 80;
  a.sample_interval = 10;
  a.seed = 880;
  DiagnosticScenario b = a;
  b.sample_interval = 40;
  const auto root = std::filesystem::path("build-bench") / "diagnostic_invariant_runs";
  DiagnosticRunResult first = run_diagnostic_experiment(a, DiagnosticOutputOptions{}, root / "sample_10");
  DiagnosticRunResult second = run_diagnostic_experiment(b, DiagnosticOutputOptions{}, root / "sample_40");

  const std::string h1 = read_file(first.output_dir / "run_hash.json");
  const std::string h2 = read_file(second.output_dir / "run_hash.json");
  EXPECT_EQ(json_string(h1, "agent_final_state_hash"), json_string(h2, "agent_final_state_hash"));
  EXPECT_EQ(json_string(h1, "accounting_summary_hash"), json_string(h2, "accounting_summary_hash"));
  EXPECT_EQ(first.final_mid_price, second.final_mid_price);
  EXPECT_EQ(first.system_pnl_residual, second.system_pnl_residual);
}
