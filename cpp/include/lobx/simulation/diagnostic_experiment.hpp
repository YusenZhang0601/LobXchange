#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace lobx::simulation {

struct DiagnosticScenario {
  std::string name;
  std::uint64_t seed{202501};
  int agent_count{50};
  int steps{50000};
  int sample_interval{10};
  int book_depth{10};
  double static_market_maker_ratio{0.0};
  double noise_trader_ratio{0.0};
  double momentum_follower_ratio{0.0};
  double mean_reverter_ratio{0.0};
  double whale_sweeper_ratio{0.0};
  bool bounded_quotes{false};
  int max_open_orders_per_side{1};
  int quote_refresh_interval_steps{10};
  int quote_ttl_steps{20};
  bool cancel_stale_quotes{true};
  bool replace_on_price_change{true};
  bool enable_scheduler{false};
  int static_market_maker_interval{10};
  int noise_trader_interval{5};
  int momentum_follower_interval{5};
  int mean_reverter_interval{10};
  int whale_sweeper_interval{12};
};

struct DiagnosticOutputOptions {
  bool jsonl_events{false};
  bool agent_snapshots{true};
  bool order_trace{false};
  bool impact_windows{false};
};

struct DiagnosticRunResult {
  std::filesystem::path output_dir;
  int price_sample_count{0};
  int agent_final_state_count{0};
  double final_mid_price{0.0};
  double system_pnl_residual{0.0};
};

DiagnosticScenario balanced_mixed_agents_long();
DiagnosticScenario stress_whale_momentum_long();
DiagnosticScenario market_maker_inventory_pressure_long();
DiagnosticScenario balanced_mixed_agents_bounded_long();
DiagnosticScenario stress_whale_momentum_bounded_long();

DiagnosticScenario apply_diagnostic_env_overrides(DiagnosticScenario scenario);
DiagnosticOutputOptions diagnostic_output_options_from_env();
std::filesystem::path diagnostic_output_root_from_env();

DiagnosticRunResult run_diagnostic_experiment(const DiagnosticScenario& scenario,
                                              const DiagnosticOutputOptions& options,
                                              const std::filesystem::path& output_root);

} // namespace lobx::simulation
