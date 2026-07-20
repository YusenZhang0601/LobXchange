#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "lobx/simulation/agent_runtime.hpp"

namespace lobx::simulation {

struct PriceSeriesSample {
  int step{0};
  lobx::agents::Timestamp ts{0};
  double best_bid{0.0};
  double best_ask{0.0};
  double mid_price{0.0};
  double spread{0.0};
  double spread_bps{0.0};
  double last_trade_price{0.0};
  double cum_volume{0.0};
  int trade_count{0};
};

struct PriceImpactSummary {
  std::string scenario;
  std::uint64_t seed{0};
  int agent_count{0};
  int steps{0};
  double initial_mid_price{0.0};
  double final_mid_price{0.0};
  double total_return_bps{0.0};
  double realized_vol_bps{0.0};
  double max_drawdown_bps{0.0};
  double largest_abs_return_bps{0.0};
  int trade_count{0};
  double cum_volume{0.0};
  int price_samples_count{0};
};

class PriceSeriesRecorder {
public:
  void record(int step, const AgentRuntime& runtime);

  const std::vector<PriceSeriesSample>& samples() const { return samples_; }
  PriceImpactSummary summarize(const std::string& scenario,
                               std::uint64_t seed,
                               int agent_count,
                               int steps) const;

  bool write_price_series_csv(const std::string& path) const;
  bool write_summary_json(const std::string& path, const PriceImpactSummary& summary) const;
  bool write_summary_json(const std::string& path,
                          const PriceImpactSummary& summary,
                          const AccountingSummary& accounting) const;

private:
  std::vector<PriceSeriesSample> samples_;
};

std::string price_series_csv(const std::vector<PriceSeriesSample>& samples);
std::string price_impact_summary_json(const PriceImpactSummary& summary);
std::string accounting_summary_json(const AccountingSummary& summary);
std::string scenario_comparison_csv(const std::vector<PriceImpactSummary>& summaries);
bool write_scenario_comparison_csv(const std::string& path, const std::vector<PriceImpactSummary>& summaries);

} // namespace lobx::simulation
