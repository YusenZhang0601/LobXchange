#include "lobx/simulation/diagnostic_experiment.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>

#include "lobx/agents/agent_factory.hpp"
#include "lobx/simulation/agent_runtime.hpp"
#include "lobx/simulation/diagnostic_recorder.hpp"
#include "lobx/simulation/price_series_recorder.hpp"

namespace lobx::simulation {

namespace {

bool env_enabled(const char* name) {
  const char* value = std::getenv(name);
  return value != nullptr && std::string(value) == "1";
}

int env_int(const char* name, int fallback) {
  const char* value = std::getenv(name);
  if (value == nullptr || *value == '\0') return fallback;
  return std::max(1, std::atoi(value));
}

std::uint64_t env_u64(const char* name, std::uint64_t fallback) {
  const char* value = std::getenv(name);
  if (value == nullptr || *value == '\0') return fallback;
  return static_cast<std::uint64_t>(std::strtoull(value, nullptr, 10));
}

int count_for(const DiagnosticScenario& scenario, double ratio, int assigned_so_far, bool last) {
  const int remaining = std::max(0, scenario.agent_count - assigned_so_far);
  if (last) return remaining;
  const int requested = static_cast<int>(std::llround(static_cast<double>(scenario.agent_count) * ratio));
  return std::clamp(requested, 0, remaining);
}

void add_agents(AgentRuntime& runtime,
                lobx::agents::AgentFactoryRegistry& registry,
                const std::string& type,
                int count,
                lobx::agents::AgentId& next_agent_id,
                lobx::agents::AgentGroupId group_id,
                std::uint64_t scenario_seed,
                const DiagnosticScenario& scenario) {
  for (int i = 0; i < count; ++i) {
    lobx::agents::AgentConfig config{};
    config.type = type;
    config.group_id = group_id;
    config.numeric_params["seed"] =
        static_cast<double>(scenario_seed + next_agent_id * 1315423911ULL + static_cast<std::uint64_t>(i));
    config.numeric_params["reference_price"] = 100.0;
    if (type == "whale_sweeper") {
      config.numeric_params["interval"] = 12.0;
      config.numeric_params["quantity"] = 20.0;
    }
    if (type == "static_market_maker" && scenario.bounded_quotes) {
      config.numeric_params["bounded_quotes"] = 1.0;
      config.numeric_params["max_open_orders_per_side"] = static_cast<double>(scenario.max_open_orders_per_side);
      config.numeric_params["quote_refresh_interval_steps"] = static_cast<double>(scenario.quote_refresh_interval_steps);
      config.numeric_params["quote_ttl_steps"] = static_cast<double>(scenario.quote_ttl_steps);
      config.numeric_params["cancel_stale_quotes"] = scenario.cancel_stale_quotes ? 1.0 : 0.0;
      config.numeric_params["replace_on_price_change"] = scenario.replace_on_price_change ? 1.0 : 0.0;
    }
    runtime.add_agent(registry.create(type, next_agent_id++, config));
  }
}

} // namespace

DiagnosticScenario balanced_mixed_agents_long() {
  return DiagnosticScenario{"balanced_mixed_agents_long", 202501, 50, 50000, 10, 10, 0.40, 0.30, 0.15, 0.10, 0.05};
}

DiagnosticScenario stress_whale_momentum_long() {
  return DiagnosticScenario{"stress_whale_momentum_long", 202502, 50, 50000, 10, 10, 0.35, 0.25, 0.25, 0.05, 0.10};
}

DiagnosticScenario market_maker_inventory_pressure_long() {
  return DiagnosticScenario{"market_maker_inventory_pressure_long", 202503, 50, 50000, 10, 10, 0.70, 0.30, 0.0, 0.0, 0.0};
}

DiagnosticScenario balanced_mixed_agents_bounded_long() {
  DiagnosticScenario scenario{"balanced_mixed_agents_bounded_long", 202501, 50, 50000, 10, 10, 0.40, 0.30, 0.15, 0.10, 0.05};
  scenario.bounded_quotes = true;
  scenario.max_open_orders_per_side = 1;
  scenario.quote_refresh_interval_steps = 10;
  scenario.quote_ttl_steps = 20;
  scenario.cancel_stale_quotes = true;
  scenario.replace_on_price_change = true;
  scenario.enable_scheduler = true;
  return scenario;
}

DiagnosticScenario stress_whale_momentum_bounded_long() {
  DiagnosticScenario scenario{"stress_whale_momentum_bounded_long", 202502, 50, 50000, 10, 10, 0.35, 0.25, 0.25, 0.05, 0.10};
  scenario.bounded_quotes = true;
  scenario.max_open_orders_per_side = 1;
  scenario.quote_refresh_interval_steps = 10;
  scenario.quote_ttl_steps = 20;
  scenario.cancel_stale_quotes = true;
  scenario.replace_on_price_change = true;
  scenario.enable_scheduler = true;
  return scenario;
}

DiagnosticScenario apply_diagnostic_env_overrides(DiagnosticScenario scenario) {
  scenario.agent_count = env_int("LOBX_DIAG_AGENT_COUNT", scenario.agent_count);
  scenario.steps = env_int("LOBX_DIAG_STEPS", scenario.steps);
  scenario.seed = env_u64("LOBX_DIAG_SEED", scenario.seed);
  scenario.sample_interval = env_int("LOBX_DIAG_SAMPLE_INTERVAL", scenario.sample_interval);
  scenario.book_depth = env_int("LOBX_DIAG_BOOK_DEPTH", scenario.book_depth);
  return scenario;
}

DiagnosticOutputOptions diagnostic_output_options_from_env() {
  const bool snapshots_unset = std::getenv("LOBX_DIAG_ENABLE_AGENT_SNAPSHOTS") == nullptr;
  return DiagnosticOutputOptions{env_enabled("LOBX_DIAG_ENABLE_JSONL_EVENTS"),
                                 snapshots_unset || env_enabled("LOBX_DIAG_ENABLE_AGENT_SNAPSHOTS"),
                                 env_enabled("LOBX_DIAG_ENABLE_ORDER_TRACE"),
                                 env_enabled("LOBX_DIAG_ENABLE_IMPACT_WINDOWS")};
}

std::filesystem::path diagnostic_output_root_from_env() {
  if (const char* value = std::getenv("LOBX_DIAG_OUTPUT_DIR")) return value;
  return std::filesystem::path("build-bench") / "diagnostic_runs";
}

DiagnosticRunResult run_diagnostic_experiment(const DiagnosticScenario& scenario,
                                              const DiagnosticOutputOptions& options,
                                              const std::filesystem::path& output_root) {
  const std::filesystem::path output_dir = output_root / scenario.name / ("seed=" + std::to_string(scenario.seed));
  std::filesystem::remove_all(output_dir);
  std::filesystem::create_directories(output_dir);

  AgentRuntimeConfig runtime_config{};
  runtime_config.steps = scenario.steps;
  runtime_config.reference_price = 100;
  runtime_config.book_levels = scenario.book_depth;
  runtime_config.recent_trade_limit = 512;
  runtime_config.retain_action_trace = options.jsonl_events;
  runtime_config.retain_events = options.jsonl_events;
  runtime_config.enable_scheduler = scenario.enable_scheduler;
  runtime_config.default_decision_interval_steps = 1;
  if (scenario.enable_scheduler) {
    runtime_config.decision_interval_by_type[lobx::agents::AgentType::StaticMarketMaker] =
        std::max(1, scenario.static_market_maker_interval);
    runtime_config.decision_interval_by_type[lobx::agents::AgentType::NoiseTrader] =
        std::max(1, scenario.noise_trader_interval);
    runtime_config.decision_interval_by_type[lobx::agents::AgentType::MomentumFollower] =
        std::max(1, scenario.momentum_follower_interval);
    runtime_config.decision_interval_by_type[lobx::agents::AgentType::MeanReverter] =
        std::max(1, scenario.mean_reverter_interval);
    runtime_config.decision_interval_by_type[lobx::agents::AgentType::WhaleSweeper] =
        std::max(1, scenario.whale_sweeper_interval);
  }
  AgentRuntime runtime(runtime_config);

  lobx::agents::AgentFactoryRegistry registry;
  lobx::agents::register_builtin_agents(registry);

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
  if (assigned != scenario.agent_count) throw std::runtime_error("diagnostic agent allocation mismatch");

  lobx::agents::AgentId next_agent_id = 100;
  add_agents(runtime, registry, "static_market_maker", makers, next_agent_id, 1, scenario.seed, scenario);
  add_agents(runtime, registry, "noise_trader", noise, next_agent_id, 2, scenario.seed, scenario);
  add_agents(runtime, registry, "momentum_follower", momentum, next_agent_id, 3, scenario.seed, scenario);
  add_agents(runtime, registry, "mean_reverter", mean_reversion, next_agent_id, 4, scenario.seed, scenario);
  add_agents(runtime, registry, "whale_sweeper", whales, next_agent_id, 5, scenario.seed, scenario);

  DiagnosticRecorder recorder(scenario, options, output_dir);
  if (!recorder.open()) throw std::runtime_error("failed to open diagnostic recorder outputs");
  recorder.write_metadata();
  recorder.write_headers();

  PriceSeriesRecorder price_summary_recorder;
  price_summary_recorder.record(0, runtime);
  recorder.record_step(runtime, 0);
  for (int step = 1; step <= scenario.steps; ++step) {
    runtime.step();
    if ((step % std::max(1, scenario.sample_interval)) == 0 || step == scenario.steps) {
      price_summary_recorder.record(step, runtime);
    }
    recorder.record_step(runtime, step);
  }

  PriceImpactSummary summary = price_summary_recorder.summarize(scenario.name,
                                                                scenario.seed,
                                                                scenario.agent_count,
                                                                scenario.steps);
  recorder.finalize(runtime, summary);
  return DiagnosticRunResult{output_dir,
                             recorder.price_samples_written(),
                             scenario.agent_count,
                             summary.final_mid_price,
                             runtime.accounting_summary(summary.final_mid_price).system_pnl_residual};
}

} // namespace lobx::simulation
