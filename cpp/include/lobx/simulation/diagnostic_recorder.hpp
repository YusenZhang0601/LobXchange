#pragma once

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "lobx/agents/agent_types.hpp"
#include "lobx/simulation/agent_runtime.hpp"
#include "lobx/simulation/diagnostic_experiment.hpp"
#include "lobx/simulation/price_series_recorder.hpp"

namespace lobx::simulation {

class DiagnosticRecorder {
public:
  DiagnosticRecorder(DiagnosticScenario scenario,
                     DiagnosticOutputOptions options,
                     std::filesystem::path output_dir);
  ~DiagnosticRecorder();

  bool open();
  bool is_open() const;

  void write_metadata();
  void write_headers();
  void record_step(const AgentRuntime& runtime, int step);
  void finalize(const AgentRuntime& runtime, const PriceImpactSummary& summary);

  int price_samples_written() const { return price_samples_written_; }
  int agent_samples_written() const { return agent_samples_written_; }
  int jsonl_events_written() const { return jsonl_events_written_; }
  const std::filesystem::path& output_dir() const { return output_dir_; }

private:
  struct TypeAggregate {
    int count{0};
    std::vector<double> pnl;
    double sum_pnl{0.0};
    double sum_equity{0.0};
    double sum_inventory{0.0};
    double total_buy_volume{0.0};
    double total_sell_volume{0.0};
    int total_trade_count{0};
    double sum_open_order_count{0.0};
    int negative_count{0};
    int positive_count{0};
    int zero_count{0};
  };

  void write_price_sample(const AgentRuntime& runtime, int step);
  void write_book_sample(const AgentRuntime& runtime, int step);
  void write_agent_samples(const AgentRuntime& runtime, int step);
  void write_action_samples(const AgentRuntime& runtime, int step);
  void write_trade_sample(const AgentRuntime& runtime, int step);
  void write_runtime_metrics(const AgentRuntime& runtime, int step);
  void write_open_order_growth(const AgentRuntime& runtime, int step);
  void write_agent_type_pnl_timeseries(const AgentRuntime& runtime, int step);
  void write_jsonl_events(const AgentRuntime& runtime);
  void write_empty_optional_files();
  void write_final_agent_state(const AgentRuntime& runtime, double mark_price);
  void write_agent_type_summary(const AgentRuntime& runtime, double mark_price);
  void write_inventory_consistency(const AgentRuntime& runtime, double mark_price);
  void write_perf_summary(const AgentRuntime& runtime);
  void write_run_hash(const AgentRuntime& runtime);
  void write_unit_sanity_summary(const AgentRuntime& runtime);
  void write_accounting_summary(const AgentRuntime& runtime, double mark_price);
  void write_summary(const PriceImpactSummary& summary);
  void write_warnings();

  double current_mid(const AgentRuntime& runtime) const;
  double step_return_bps(double mid);
  double drawdown_bps(double mid);
  void accumulate_runtime_stats(const AgentRuntime& runtime);
  void reset_interval_stats();

  DiagnosticScenario scenario_;
  DiagnosticOutputOptions options_;
  std::filesystem::path output_dir_;
  std::chrono::steady_clock::time_point started_;

  std::ofstream price_series_;
  std::ofstream book_samples_;
  std::ofstream agent_state_samples_;
  std::ofstream actions_by_step_;
  std::ofstream actions_by_type_;
  std::ofstream trades_by_step_;
  std::ofstream runtime_metrics_;
  std::ofstream open_order_growth_;
  std::ofstream agent_type_pnl_timeseries_;
  std::ofstream agent_actions_jsonl_;
  std::ofstream simulation_events_jsonl_;

  std::vector<std::string> warnings_;
  double previous_mid_{0.0};
  double running_peak_{0.0};
  int price_samples_written_{0};
  int agent_samples_written_{0};
  int jsonl_events_written_{0};
  std::size_t last_action_trace_index_{0};
  std::size_t last_event_trace_index_{0};
  double interval_agent_decide_ms_{0.0};
  double interval_action_schedule_ms_{0.0};
  double interval_exchange_apply_ms_{0.0};
  double interval_state_update_ms_{0.0};
  double interval_book_sample_ms_{0.0};
  double interval_recorder_ms_{0.0};
  int interval_agents_due_{0};
  int interval_agents_skipped_{0};
  int interval_agent_decisions_{0};
  double total_agent_decide_ms_{0.0};
  double total_action_schedule_ms_{0.0};
  double total_exchange_apply_ms_{0.0};
  double total_state_update_ms_{0.0};
  double total_book_sample_ms_{0.0};
  double total_recorder_ms_{0.0};
  int max_open_orders_{0};
  int max_open_orders_per_agent_{0};
};

} // namespace lobx::simulation
